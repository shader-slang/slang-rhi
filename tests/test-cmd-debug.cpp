#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

static const MarkerColor MAGENTA = {1.f, 0.f, 1.f};
static const MarkerColor RED = {1.f, 0.f, 0.f};
static const MarkerColor GREEN = {0.f, 1.f, 0.f};
static const MarkerColor BLUE = {0.f, 0.f, 1.f};

GPU_TEST_CASE("cmd-debug", ALL)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-cmd-debug", "computeMain", shaderProgram.writeRef()));

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
