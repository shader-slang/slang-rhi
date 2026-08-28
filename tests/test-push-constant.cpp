#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("explicit-push-constant", Vulkan)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-push-constant", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const uint32_t initialValue = 0;
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(uint32_t);
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopyDestination | BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> resultBuffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, &initialValue, resultBuffer.writeRef()));

    ComPtr<IShaderObject> rootObject;
    REQUIRE_CALL(device->createRootShaderObject(shaderProgram, rootObject.writeRef()));

    const uint32_t expectedValue = 0x12345678;
    ShaderCursor cursor(rootObject);
    REQUIRE_CALL(cursor["pushConstants"]["value"].setData(expectedValue));
    REQUIRE_CALL(cursor["pushConstants"]["result"].setBinding(resultBuffer));

    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();
    auto passEncoder = commandEncoder->beginComputePass();
    passEncoder->bindPipeline(pipeline, rootObject);
    passEncoder->dispatchCompute(1, 1, 1);
    passEncoder->end();

    queue->submit(commandEncoder->finish());
    queue->waitOnHost();

    compareComputeResult(device, resultBuffer, makeArray<uint32_t>(expectedValue));
}
