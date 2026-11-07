#include "testing.h"
#include "shader-cache.h"
#include "core/task-pool.h"
#include "device.h"
#include "shader.h"

#include <memory>

using namespace rhi;
using namespace rhi::testing;

namespace {

void runDeferredPipelineBatch(
    IDevice* device,
    PipelineCompilationPolicy compilationPolicy = PipelineCompilationPolicy::Default,
    bool mixDeferred = false
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
        desc.compilationPolicy = mixDeferred && i % 2 ? PipelineCompilationPolicy::Deferred : compilationPolicy;
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

    if (mixDeferred)
    {
        // Specialization becomes known only during recording, alongside early and explicitly deferred jobs.
        ComPtr<IShaderProgram> program;
        slang::ProgramLayout* reflection = nullptr;
        REQUIRE_CALL(loadAndLinkProgram(
            device,
            "test-shader-cache-specialization",
            "computeMain",
            program.writeRef(),
            &reflection
        ));
        ComputePipelineDesc desc = {};
        desc.program = program;
        desc.compilationPolicy = PipelineCompilationPolicy::Async;
        auto pipeline = device->createComputePipeline(desc);
        REQUIRE(pipeline);
        ComPtr<IShaderObject> transformer;
        REQUIRE_CALL(device->createShaderObject(
            nullptr,
            reflection->findTypeByName("AddTransformer"),
            ShaderObjectContainerType::None,
            transformer.writeRef()
        ));
        ShaderCursor(transformer)["c"].setData(0.f);
        REQUIRE_CALL(transformer->finalize());
        auto pass = encoder->beginComputePass();
        auto root = pass->bindPipeline(pipeline);
        ShaderCursor entryPoint(root->getEntryPoint(0));
        entryPoint["buffer"].setBinding(buffer);
        entryPoint["transformer"].setObject(transformer);
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

// A deterministic scheduler lets tests inspect submission without racing real compiler work.
// Handles and the queue own tasks independently, just like the production threaded pool.
class QueuedTaskPool : public BlockingTaskPool
{
public:
    struct Task
    {
        void (*func)(void*);
        void* payload;
        void (*deleter)(void*);
        bool done = false;
    };

    std::vector<std::shared_ptr<Task>> tasks;

    TaskHandle SLANG_MCALL submitTask(
        void (*func)(void*),
        void* payload,
        void (*deleter)(void*),
        TaskGroupHandle group = nullptr
    ) override
    {
        CHECK(group == nullptr);
        auto task = std::make_shared<Task>(Task{func, payload, deleter});
        tasks.push_back(task);
        return new std::shared_ptr<Task>(task);
    }

    void SLANG_MCALL releaseTask(TaskHandle handle) override { delete static_cast<std::shared_ptr<Task>*>(handle); }

    static void run(Task& task)
    {
        if (task.done)
            return;
        task.done = true;
        task.func(task.payload);
        if (task.deleter)
            task.deleter(task.payload);
    }

    void SLANG_MCALL waitAndReleaseTask(TaskHandle handle) override
    {
        auto task = *static_cast<std::shared_ptr<Task>*>(handle);
        run(*task);
        releaseTask(handle);
    }

    void runAll()
    {
        for (size_t i = 0; i < tasks.size(); ++i)
        {
            auto task = tasks[i];
            run(*task);
        }
    }
};

struct QueuedTaskPoolReset
{
    ComPtr<QueuedTaskPool> pool;
    ComPtr<IDevice>& device;

    ~QueuedTaskPoolReset()
    {
        pool->runAll();
        device.setNull();
        CHECK(SLANG_SUCCEEDED(getRHI()->initTaskPool(-1)));
    }
};

class FailingShaderProgram : public ShaderProgram
{
public:
    RefPtr<ShaderProgram> source;
    uint32_t compileCount = 0;

    FailingShaderProgram(Device* device, ShaderProgram* source_)
        : ShaderProgram(device, source_->m_desc)
        , source(source_)
    {
    }

    ShaderObjectLayout* getRootShaderObjectLayout() override { return source->getRootShaderObjectLayout(); }

    Result compileEntryPoint(Device*, CompiledEntryPoint&, bool) override
    {
        ++compileCount;
        return SLANG_FAIL;
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

GPU_TEST_CASE("async-pipeline-creation", ALL | DontCreateDevice)
{
    DeviceExtraOptions options;
    options.pipelineCompilationMode = PipelineCompilationMode::Async;
    device = createTestingDevice(ctx, ctx->deviceType, false, &options);
    REQUIRE(device);
    runDeferredPipelineBatch(device);
    runDeferredPipelineBatch(device, PipelineCompilationPolicy::Default, true);
}

GPU_TEST_CASE("async-pipeline-creation-single-worker", ALL | DontCreateDevice)
{
    releaseCachedDevices();
    REQUIRE_CALL(getRHI()->initTaskPool(1));
    TaskPoolReset reset{device};
    DeviceExtraOptions options;
    options.pipelineCompilationMode = PipelineCompilationMode::Parallel;
    device = createTestingDevice(ctx, ctx->deviceType, false, &options);
    REQUIRE(device);
    runDeferredPipelineBatch(device, PipelineCompilationPolicy::Async, true);
}

GPU_TEST_CASE("async-pipeline-creation-blocking-pool", ALL | DontCreateDevice)
{
    releaseCachedDevices();
    REQUIRE_CALL(getRHI()->initTaskPool(0));
    TaskPoolReset reset{device};
    DeviceExtraOptions options;
    options.pipelineCompilationMode = PipelineCompilationMode::Serial;
    device = createTestingDevice(ctx, ctx->deviceType, false, &options);
    REQUIRE(device);
    runDeferredPipelineBatch(device, PipelineCompilationPolicy::Async);
}

GPU_TEST_CASE("async-pipeline-creation-sharing-and-lifetime", D3D12 | Vulkan | DontCreateDevice)
{
    releaseCachedDevices();
    ComPtr<QueuedTaskPool> pool(new QueuedTaskPool());
    REQUIRE_CALL(getRHI()->setTaskPool(pool));
    QueuedTaskPoolReset reset{pool, device};
    DeviceExtraOptions options;
    options.pipelineCompilationMode = PipelineCompilationMode::Async;
    options.enableCompilationReports = true;
    device = createTestingDevice(ctx, ctx->deviceType, false, &options);
    REQUIRE(device);

    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-parallel-pipeline-creation", "computeAdd", program.writeRef()));
    ComputePipelineDesc desc = {};
    desc.program = program;
    ComPtr<IComputePipeline> first, second;
    REQUIRE_CALL(device->createComputePipeline(desc, first.writeRef()));
    REQUIRE(pool->tasks.size() == 1);
    CHECK_FALSE(pool->tasks[0]->done);
    REQUIRE_CALL(device->createComputePipeline(desc, second.writeRef()));
    CHECK(pool->tasks.size() == 1);

    // Immediate creation joins the pending code generation instead of compiling it again.
    desc.compilationPolicy = PipelineCompilationPolicy::Immediate;
    ComPtr<IComputePipeline> immediate;
    REQUIRE_CALL(device->createComputePipeline(desc, immediate.writeRef()));
    CHECK(pool->tasks[0]->done);
    CHECK(pool->tasks.size() == 1);
    ComPtr<ISlangBlob> reportBlob;
    REQUIRE_CALL(program->getCompilationReport(reportBlob.writeRef()));
    CHECK(static_cast<const CompilationReport*>(reportBlob->getBufferPointer())->entryPointReportCount == 1);

    // Dropping an unused pipeline/program/device must leave queued work valid and must not leak a device.
    immediate.setNull();
    second.setNull();
    first.setNull();
    program.setNull();
    REQUIRE_CALL(loadProgram(device, "test-parallel-pipeline-creation", "computeMul", program.writeRef()));
    desc.program = program;
    desc.compilationPolicy = PipelineCompilationPolicy::Async;
    REQUIRE_CALL(device->createComputePipeline(desc, first.writeRef()));
    REQUIRE(pool->tasks.size() == 2);
    CHECK_FALSE(pool->tasks[1]->done);
    first.setNull();
    program.setNull();
    device.setNull();
    pool->runAll();
    CHECK(pool->tasks[1]->done);
}

GPU_TEST_CASE("async-pipeline-creation-failure", D3D12 | Vulkan | DontCreateDevice)
{
    releaseCachedDevices();
    ComPtr<QueuedTaskPool> pool(new QueuedTaskPool());
    REQUIRE_CALL(getRHI()->setTaskPool(pool));
    QueuedTaskPoolReset reset{pool, device};
    DeviceExtraOptions options;
    options.pipelineCompilationMode = PipelineCompilationMode::Async;
    device = createTestingDevice(ctx, ctx->deviceType, false, &options);
    REQUIRE(device);

    auto impl = static_cast<Device*>(getUnderlyingDevice(device));
    ComPtr<IShaderProgram> source;
    REQUIRE_CALL(loadComputeProgramFromSource(
        impl,
        "[shader(\"compute\")] [numthreads(1,1,1)] void computeMain() {}",
        source.writeRef()
    ));
    RefPtr<FailingShaderProgram> program = new FailingShaderProgram(impl, static_cast<ShaderProgram*>(source.get()));
    REQUIRE_CALL(program->init());
    ComputePipelineDesc desc = {};
    desc.program = program;
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(impl->createComputePipeline(desc, pipeline.writeRef()));
    CHECK(program->compileCount == 0);

    // Both repeated resolution and immediate creation must return the stored failure without retrying codegen.
    for (int i = 0; i < 2; ++i)
    {
        auto encoder = impl->getQueue(QueueType::Graphics)->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        pass->bindPipeline(pipeline);
        pass->dispatchCompute(1, 1, 1);
        pass->end();
        ComPtr<ICommandBuffer> commands;
        CHECK(SLANG_FAILED(encoder->finish(commands.writeRef())));
        CHECK(program->compileCount == 1);
    }
    desc.compilationPolicy = PipelineCompilationPolicy::Immediate;
    ComPtr<IComputePipeline> immediate;
    CHECK(SLANG_FAILED(impl->createComputePipeline(desc, immediate.writeRef())));
    CHECK(program->compileCount == 1);
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
