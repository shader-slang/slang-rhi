#include "testing.h"
#include "shader-cache.h"

using namespace rhi;
using namespace rhi::testing;

namespace {

void runDeferredPipelineBatch(IDevice* device)
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
        desc.deferTargetCompilation = true;
        REQUIRE_CALL(device->createComputePipeline(desc, pipelines[i].writeRef()));
    }

    // A second virtual pipeline sharing the add program exercises program-level deduplication.
    ComputePipelineDesc secondAddDesc = {};
    secondAddDesc.program = programs[0];
    secondAddDesc.deferTargetCompilation = true;
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
        renderPipelineDesc.deferTargetCompilation = true;

        // WGPU currently requires an input layout even when the vertex shader has no vertex inputs.
        ComPtr<IInputLayout> inputLayout;
        if (device->getDeviceType() == DeviceType::WGPU)
        {
            InputLayoutDesc inputLayoutDesc = {};
            REQUIRE_CALL(device->createInputLayout(inputLayoutDesc, inputLayout.writeRef()));
            renderPipelineDesc.inputLayout = inputLayout;
        }
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

} // namespace

GPU_TEST_CASE("parallel-pipeline-creation", ALL | DontCreateDevice)
{
    DeviceExtraOptions options;
    options.pipelineCompilationMode = PipelineCompilationMode::Parallel;
    device = createTestingDevice(ctx, ctx->deviceType, false, &options);
    REQUIRE(device);
    runDeferredPipelineBatch(device);
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
    runDeferredPipelineBatch(device);
}
