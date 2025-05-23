#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

static const MarkerColor MAGENTA = {1.f, 0.f, 1.f};
static const MarkerColor RED = {1.f, 0.f, 0.f};
static const MarkerColor GREEN = {0.f, 1.f, 0.f};
static const MarkerColor BLUE = {0.f, 0.f, 1.f};

GPU_TEST_CASE("cmd-debug-compute-pass", ALL)
{
    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection;
    REQUIRE_CALL(loadComputeProgram(device, shaderProgram, "test-cmd-debug", "computeMain", slangReflection));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    float initialData[] = {0.f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(float);
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initialData, buffer.writeRef()));

    {
        renderDocBeginFrame();

        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor shaderCursor(rootObject);
        shaderCursor["buffer"].setBinding(buffer);
        passEncoder->pushDebugGroup("Compute", MAGENTA);
        passEncoder->insertDebugMarker("Dispatch 1", RED);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->insertDebugMarker("Dispatch 2", GREEN);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->insertDebugMarker("Dispatch 3", BLUE);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->popDebugGroup();
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();

        renderDocEndFrame();
    }

    float result;
    REQUIRE_CALL(device->readBuffer(buffer, 0, sizeof(float), &result));
    REQUIRE(result == 3.f);
}

GPU_TEST_CASE("cmd-debug-render-pass", ALL)
{
    if (!device->hasFeature(Feature::Rasterization))
    {
        SKIP("Rasterization feature not supported");
    }

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection;
    REQUIRE_CALL(loadGraphicsProgram(device, shaderProgram, "test-cmd-debug", "vertexMain", "fragmentMain", slangReflection));

    RenderPipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IRenderPipeline> pipeline;
    REQUIRE_CALL(device->createRenderPipeline(pipelineDesc, pipeline.writeRef()));

    float initialData[] = {0.f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(float);
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initialData, buffer.writeRef()));

    {
        renderDocBeginFrame();

        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        RenderPassDesc renderPassDesc = {};
        auto passEncoder = commandEncoder->beginRenderPass(renderPassDesc);

        RenderState renderState = {};
        renderState.viewportCount = 1;
        renderState.viewports[0] = Viewport::fromSize(1.f, 1.f);
        renderState.scissorRectCount = 1;
        renderState.scissorRects[0] = ScissorRect::fromSize(1.f, 1.f);
        passEncoder->setRenderState(renderState);

        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor shaderCursor(rootObject);
        shaderCursor["buffer"].setBinding(buffer);
        DrawArguments drawArgs = {};
        passEncoder->pushDebugGroup("Compute", MAGENTA);
        passEncoder->insertDebugMarker("Dispatch 1", RED);
        passEncoder->draw(drawArgs);
        passEncoder->insertDebugMarker("Dispatch 2", GREEN);
        passEncoder->draw(drawArgs);
        passEncoder->insertDebugMarker("Dispatch 3", BLUE);
        passEncoder->draw(drawArgs);
        passEncoder->popDebugGroup();
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();

        renderDocEndFrame();
    }

    float result;
    REQUIRE_CALL(device->readBuffer(buffer, 0, sizeof(float), &result));
    // REQUIRE(result == 3.f);
}
