#include "testing.h"
#include "shader-cache.h"
#include "device.h"
#include "core/sha1.h"

#if SLANG_RHI_ENABLE_CUDA
#include "cuda/cuda-device.h"
#endif

#include <atomic>

using namespace rhi;
using namespace rhi::testing;

namespace {

void runDeferredPipelineBatch(
    IDevice* device,
    PipelineCompilationPolicy compilationPolicy = PipelineCompilationPolicy::Default,
    bool resolveBeforeRecording = false
)
{
    const char* entryPointNames[] = {"computeAdd", "computeMul", "computeSub", "computeNeg"};
    ComPtr<IShaderProgram> programs[4];
    ComPtr<IComputePipeline> pipelines[5];

    for (size_t i = 0; i < std::size(programs); ++i)
    {
        REQUIRE_CALL(
            loadProgram(device, "test-parallel-pipeline-creation", entryPointNames[i], programs[i].writeRef())
        );

        ComputePipelineDesc desc = {};
        desc.program = programs[i];
        desc.compilationPolicy = compilationPolicy;
        REQUIRE_CALL(device->createComputePipeline(desc, pipelines[i].writeRef()));

        NativeHandle nativeHandle = {};
        CHECK(pipelines[i]->getNativeHandle(&nativeHandle) == SLANG_E_NOT_AVAILABLE);
    }

    // A second virtual pipeline sharing the add program exercises program-level deduplication.
    ComputePipelineDesc secondAddDesc = {};
    secondAddDesc.program = programs[0];
    secondAddDesc.compilationPolicy = compilationPolicy;
    REQUIRE_CALL(device->createComputePipeline(secondAddDesc, pipelines[4].writeRef()));

    // A two-entry-point graphics program exercises concurrent getEntryPointCode calls on one linked component.
    ComPtr<IShaderProgram> renderProgram;
    ComPtr<IRenderPipeline> renderPipeline;
    ComPtr<ITexture> colorTexture;
    ComPtr<ITextureView> colorView;
    if (device->hasFeature(Feature::Rasterization))
    {
        REQUIRE_CALL(loadProgram(
            device,
            "test-parallel-pipeline-creation-render",
            {"vertexMain", "fragmentMain"},
            renderProgram.writeRef()
        ));
        ColorTargetDesc colorTarget = {};
        colorTarget.format = Format::RGBA8Unorm;
        RenderPipelineDesc renderPipelineDesc = {};
        renderPipelineDesc.program = renderProgram;
        renderPipelineDesc.targets = &colorTarget;
        renderPipelineDesc.targetCount = 1;
        renderPipelineDesc.compilationPolicy = compilationPolicy;

        REQUIRE_CALL(device->createRenderPipeline(renderPipelineDesc, renderPipeline.writeRef()));

        TextureDesc colorTextureDesc = {};
        colorTextureDesc.type = TextureType::Texture2D;
        colorTextureDesc.size = {4, 4, 1};
        colorTextureDesc.format = Format::RGBA8Unorm;
        colorTextureDesc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
        colorTextureDesc.defaultState = ResourceState::RenderTarget;
        colorTexture = device->createTexture(colorTextureDesc, nullptr);
        REQUIRE(colorTexture);
        TextureViewDesc colorViewDesc = {};
        colorViewDesc.format = Format::RGBA8Unorm;
        REQUIRE_CALL(device->createTextureView(colorTexture, colorViewDesc, colorView.writeRef()));
    }

    if (resolveBeforeRecording)
    {
        REQUIRE_CALL(device->createPendingPipelines());
        CHECK(getUnderlyingDevice(device)->m_pendingPipelines.empty());
        REQUIRE_CALL(device->createPendingPipelines());
    }

    float initialData[] = {1.0f, 2.0f, 3.0f, 4.0f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(initialData);
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initialData, buffer.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();

    // Reusing pipelines[0] exercises pipeline-level deduplication in the resolver.
    IComputePipeline* dispatchPipelines[] = {
        pipelines[0],
        pipelines[4],
        pipelines[0],
        pipelines[1],
        pipelines[2],
        pipelines[3],
    };
    for (IComputePipeline* pipeline : dispatchPipelines)
    {
        auto pass = encoder->beginComputePass();
        auto rootObject = pass->bindPipeline(pipeline);
        ShaderCursor(rootObject)["buffer"].setBinding(buffer);
        pass->dispatchCompute(1, 1, 1);
        pass->end();
    }

    if (renderPipeline)
    {
        RenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = colorView;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.storeOp = StoreOp::Store;
        RenderPassDesc renderPassDesc = {};
        renderPassDesc.colorAttachments = &colorAttachment;
        renderPassDesc.colorAttachmentCount = 1;
        auto renderPass = encoder->beginRenderPass(renderPassDesc);
        renderPass->bindPipeline(renderPipeline);
        RenderState renderState = {};
        renderState.viewports[0] = Viewport::fromSize(4, 4);
        renderState.viewportCount = 1;
        renderState.scissorRects[0] = ScissorRect::fromSize(4, 4);
        renderState.scissorRectCount = 1;
        renderPass->setRenderState(renderState);
        DrawArguments drawArgs = {};
        drawArgs.vertexCount = 3;
        renderPass->draw(drawArgs);
        renderPass->end();
    }

    ComPtr<ICommandBuffer> commandBuffer;
    REQUIRE_CALL(encoder->finish(commandBuffer.writeRef()));
    REQUIRE_CALL(queue->submit(commandBuffer));
    REQUIRE_CALL(queue->waitOnHost());

    compareComputeResult(device, buffer, makeArray<float>(-7.5f, -9.5f, -11.5f, -13.5f));

    if (colorTexture)
    {
        std::array<uint8_t, 4 * 4 * 4> expectedPixels = {};
        for (size_t i = 0; i < expectedPixels.size(); i += 4)
        {
            expectedPixels[i + 0] = 255;
            expectedPixels[i + 3] = 255;
        }
        compareComputeResult(device, colorTexture, 0, 0, expectedPixels);
    }
}

void runDeferredCudaRayTracingPipelineBatch(IDevice* device)
{
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-ray-tracing-raygen-entrypoint", {"rayGenA", "rayGenB"}, program.writeRef()));

    ComPtr<IRayTracingPipeline> pipelines[2];
    for (auto& pipeline : pipelines)
    {
        RayTracingPipelineDesc pipelineDesc = {};
        pipelineDesc.program = program;
        REQUIRE_CALL(device->createRayTracingPipeline(pipelineDesc, pipeline.writeRef()));
    }

    ComPtr<IShaderTable> shaderTable;
    ShaderTableDesc shaderTableDesc = {};
    shaderTableDesc.program = program;
    const char* rayGenNames[] = {"rayGenA", "rayGenB"};
    shaderTableDesc.rayGenShaderCount = SLANG_COUNT_OF(rayGenNames);
    shaderTableDesc.rayGenShaderEntryPointNames = rayGenNames;
    REQUIRE_CALL(device->createShaderTable(shaderTableDesc, shaderTable.writeRef()));

    ComPtr<IBuffer> outputBuffers[2];
    for (auto& outputBuffer : outputBuffers)
    {
        BufferDesc bufferDesc = {};
        bufferDesc.size = 4 * sizeof(uint32_t);
        bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, outputBuffer.writeRef()));
    }

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    for (uint32_t i = 0; i < std::size(pipelines); ++i)
    {
        auto pass = encoder->beginRayTracingPass();
        auto rootObject = pass->bindPipeline(pipelines[i], shaderTable);
        auto entryPoint = ShaderCursor(rootObject->getEntryPoint(i));
        entryPoint["output"].setBinding(outputBuffers[i]);
        entryPoint["value"].setData<uint32_t>(i == 0 ? 1 : 10);
        pass->dispatchRays(i, 2, 2, 1);
        pass->end();
    }

    ComPtr<ICommandBuffer> commandBuffer;
    REQUIRE_CALL(encoder->finish(commandBuffer.writeRef()));
    REQUIRE_CALL(queue->submit(commandBuffer));
    REQUIRE_CALL(queue->waitOnHost());

    compareComputeResult(device, outputBuffers[0], std::array<uint32_t, 4>{1, 2, 3, 4});
    compareComputeResult(device, outputBuffers[1], std::array<uint32_t, 4>{10, 12, 14, 16});
}

void checkImmediateComputePipeline(IDevice* device, PipelineCompilationPolicy compilationPolicy)
{
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-parallel-pipeline-creation", "computeAdd", program.writeRef()));

    ComputePipelineDesc desc = {};
    desc.program = program;
    desc.compilationPolicy = compilationPolicy;
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(desc, pipeline.writeRef()));

    NativeHandle nativeHandle = {};
    REQUIRE_CALL(pipeline->getNativeHandle(&nativeHandle));
}

struct TaskPoolReset
{
    ComPtr<IDevice>& device;

    ~TaskPoolReset()
    {
        device.setNull();
        CHECK(SLANG_SUCCEEDED(getRHI()->initTaskPool(-1)));
    }
};

} // namespace

GPU_TEST_CASE("parallel-pipeline-creation", ALL | DontCreateDevice)
{
    DeviceExtraOptions options;
    options.pipelineCompilationMode = PipelineCompilationMode::Parallel;
    device = createTestingDevice(ctx, ctx->deviceType, false, &options);
    REQUIRE(device);
    runDeferredPipelineBatch(device);
}

GPU_TEST_CASE("explicit-pending-pipeline-creation", ALL | DontCreateDevice)
{
    for (auto mode : {PipelineCompilationMode::Serial, PipelineCompilationMode::Parallel})
    {
        DeviceExtraOptions options;
        options.pipelineCompilationMode = mode;
        device = createTestingDevice(ctx, ctx->deviceType, false, &options);
        REQUIRE(device);
        REQUIRE_CALL(device->createPendingPipelines());

        ComPtr<IShaderProgram> program;
        REQUIRE_CALL(loadProgram(device, "test-parallel-pipeline-creation", "computeAdd", program.writeRef()));
        ComputePipelineDesc desc = {};
        desc.program = program;
        desc.compilationPolicy = PipelineCompilationPolicy::Deferred;
        ComPtr<IComputePipeline> discarded;
        REQUIRE_CALL(device->createComputePipeline(desc, discarded.writeRef()));
        CHECK(getUnderlyingDevice(device)->m_pendingPipelines.size() == 1);
        discarded.setNull();
        CHECK(getUnderlyingDevice(device)->m_pendingPipelines.empty());
        REQUIRE_CALL(device->createPendingPipelines());

        runDeferredPipelineBatch(device, PipelineCompilationPolicy::Deferred, true);
        CHECK(getUnderlyingDevice(device)->m_pendingPipelines.empty());
    }
}

GPU_TEST_CASE("parallel-pipeline-creation-shared-cache", D3D12 | Vulkan | DontCreateDevice)
{
    rhi::testing::ShaderCache sharedCache;
    DeviceExtraOptions options;
    options.persistentShaderCache = &sharedCache;
    options.persistentPipelineCache = &sharedCache;
    options.pipelineCompilationMode = PipelineCompilationMode::Parallel;
    device = createTestingDevice(ctx, ctx->deviceType, false, &options);
    REQUIRE(device);

    runDeferredPipelineBatch(device);

    // The cache is test-owned, so release the device's cache reference before the cache goes out of scope.
    device.setNull();
}

GPU_TEST_CASE("serial-pipeline-creation", ALL | DontCreateDevice)
{
    DeviceExtraOptions options;
    options.pipelineCompilationMode = PipelineCompilationMode::Serial;
    device = createTestingDevice(ctx, ctx->deviceType, false, &options);
    REQUIRE(device);
    runDeferredPipelineBatch(device, PipelineCompilationPolicy::Deferred);
}

GPU_TEST_CASE("parallel-pipeline-creation-immediate", D3D12 | Vulkan | DontCreateDevice)
{
    DeviceExtraOptions options;
    options.pipelineCompilationMode = PipelineCompilationMode::Parallel;
    device = createTestingDevice(ctx, ctx->deviceType, false, &options);
    REQUIRE(device);

    checkImmediateComputePipeline(device, PipelineCompilationPolicy::Immediate);
}

GPU_TEST_CASE("serial-pipeline-creation-default", D3D12 | Vulkan | DontCreateDevice)
{
    DeviceExtraOptions options;
    options.pipelineCompilationMode = PipelineCompilationMode::Serial;
    device = createTestingDevice(ctx, ctx->deviceType, false, &options);
    REQUIRE(device);

    checkImmediateComputePipeline(device, PipelineCompilationPolicy::Default);
}

GPU_TEST_CASE("parallel-pipeline-creation-cuda-ray-tracing", CUDA | DontCreateDevice)
{
    // Two pipeline tasks saturate the single worker plus the waiting caller.
    // Both then wait on dynamically spawned OptiX task-group work.
    releaseCachedDevices();
    REQUIRE_CALL(getRHI()->initTaskPool(1));
    TaskPoolReset taskPoolReset{device};

    DeviceExtraOptions options;
    options.pipelineCompilationMode = PipelineCompilationMode::Parallel;
    device = createTestingDevice(ctx, ctx->deviceType, false, &options);
    REQUIRE(device);
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    runDeferredCudaRayTracingPipelineBatch(device);
}

#if SLANG_RHI_ENABLE_CUDA
GPU_TEST_CASE("cuda-cubin-cache-recovery", CUDA | DontCreateDevice)
{
    // Run with SLANG_RHI_CUDA_CUBIN=1 in a build with the optional compiler.
    // The existing batch dispatches and validates four different programs.
    class Cache : public rhi::testing::ShaderCache
    {
    public:
        std::atomic<unsigned> writes = 0;
        int corruption = 0;
        bool failIO = false;

        Result SLANG_MCALL writeCache(ISlangBlob* key, ISlangBlob* value) override
        {
            ++writes;
            if (failIO)
                return SLANG_FAIL;
            return rhi::testing::ShaderCache::writeCache(key, value);
        }

        Result SLANG_MCALL queryCache(ISlangBlob* key, ISlangBlob** outValue) override
        {
            if (failIO)
                return SLANG_FAIL;
            ComPtr<ISlangBlob> value;
            SLANG_RETURN_ON_FAIL(rhi::testing::ShaderCache::queryCache(key, value.writeRef()));
            if (corruption)
            {
                const auto* data = static_cast<const uint8_t*>(value->getBufferPointer());
                std::vector<uint8_t> bytes(data, data + value->getBufferSize());
                REQUIRE(bytes.size() > 100);
                if (corruption == 1)
                    bytes.resize(12); // Truncated record.
                else if (corruption == 2)
                    bytes.back() ^= 0xff; // Valid header, invalid checksum.
                else
                {
                    // Preserve the envelope/checksum, but make ELF e_machine invalid.
                    // This passes cache decoding and exercises rejection by CUDA itself.
                    bytes[28 + 18] = 0;
                    bytes[28 + 19] = 0;
                    auto digest = SHA1(bytes.data() + 28, bytes.size() - 28).getDigest();
                    std::memcpy(bytes.data() + 8, digest.data(), digest.size());
                }
                value = OwnedBlob::create(bytes.data(), bytes.size());
            }
            *outValue = value.detach();
            return SLANG_OK;
        }
    } cache;

    DeviceExtraOptions options;
    options.persistentPipelineCache = &cache;
    options.pipelineCompilationMode = PipelineCompilationMode::Parallel;
    {
        auto probe = createTestingDevice(ctx, DeviceType::CUDA, false, &options);
        REQUIRE(probe);
        auto cudaDevice = static_cast<rhi::cuda::DeviceImpl*>(getUnderlyingDevice(probe));
        if (!cudaDevice->m_cubinCompiler)
            SKIP("Enable SLANG_RHI_CUDA_CUBIN to test the optional CUBIN compiler.");
    }
    auto run = [&]()
    {
        // A local device ensures the test-owned cache outlives its reference,
        // including assertion failures or a skip.
        auto testDevice = createTestingDevice(ctx, DeviceType::CUDA, false, &options);
        REQUIRE(testDevice);
        runDeferredPipelineBatch(testDevice, PipelineCompilationPolicy::Deferred, true);
    };
    run();
    CHECK(cache.writes == 4);
    run();
    CHECK(cache.writes == 4); // Reused binaries must not be recompiled/written.
    for (int corruption = 1; corruption <= 3; ++corruption)
    {
        cache.corruption = corruption;
        unsigned before = cache.writes;
        run();
        // The batch shares one program between two pipelines. Driver rejection
        // can rebuild that one twice; malformed records rebuild once per program.
        CHECK(cache.writes >= before + 4);
        cache.corruption = 0;
        before = cache.writes;
        run();
        CHECK(cache.writes == before);
    }
    cache.failIO = true;
    run(); // Cache read/write errors must not prevent correct execution.
}
#endif
