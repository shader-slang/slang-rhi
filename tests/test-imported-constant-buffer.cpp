#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("imported-constant-buffer", ALL & ~CPU)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-imported-constant-buffer", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    float initialData[] = {0.0f, 0.0f, 0.0f, 0.0f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(initialData);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(float) * 4;
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> resultBuffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initialData, resultBuffer.writeRef()));

    ComPtr<IShaderObject> rootObject;
    REQUIRE_CALL(device->createRootShaderObject(shaderProgram, rootObject.writeRef()));
    ShaderCursor cursor(rootObject);

    struct Float4
    {
        float x, y, z, w;
    };

    auto slangReflection = shaderProgram->findTypeByName("MyData");
    REQUIRE(slangReflection);

    ComPtr<IShaderObject> dataObject;
    REQUIRE_CALL(device->createShaderObject(
        nullptr,
        slangReflection,
        ShaderObjectContainerType::None,
        dataObject.writeRef()
    ));
    {
        ShaderCursor dataCursor(dataObject);
        dataCursor["value"].setData(Float4{1.0f, 2.0f, 3.0f, 4.0f});
        dataObject->finalize();
    }

    cursor["gData"].setObject(dataObject);
    cursor["resultBuffer"].setBinding(resultBuffer);

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        passEncoder->bindPipeline(pipeline, rootObject);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, resultBuffer, makeArray<float>(1.0f, 2.0f, 3.0f, 4.0f));
}
