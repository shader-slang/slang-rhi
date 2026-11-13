#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

// Runs without the debug layer, which reports modifications of finalized objects
// as validation errors, so that the backend's own checks are exercised.
GPU_TEST_CASE_EX("shader-object-finalize", ALL, DebugLayerOptions{})
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-compute-trivial", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    float initialData[] = {0.0f, 1.0f, 2.0f, 3.0f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(initialData);
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initialData, buffer.writeRef()));

    ComPtr<IShaderObject> rootObject;
    REQUIRE_CALL(device->createRootShaderObject(shaderProgram, rootObject.writeRef()));
    ShaderCursor cursor(rootObject);
    REQUIRE_CALL(cursor["buffer"].setBinding(buffer));
    float value = 10.f;
    REQUIRE_CALL(cursor["value"].setData(value));

    CHECK_FALSE(rootObject->isFinalized());
    REQUIRE_CALL(rootObject->finalize());
    CHECK(rootObject->isFinalized());
    for (uint32_t i = 0; i < rootObject->getEntryPointCount(); ++i)
    {
        ComPtr<IShaderObject> entryPoint;
        REQUIRE_CALL(rootObject->getEntryPoint(i, entryPoint.writeRef()));
        CHECK(entryPoint->isFinalized());
    }

    // A finalized object rejects further modification and a second finalize.
    float otherValue = 20.f;
    CHECK(SLANG_FAILED(cursor["value"].setData(otherValue)));
    CHECK(SLANG_FAILED(cursor["buffer"].setBinding(buffer)));
    CHECK(SLANG_FAILED(rootObject->finalize()));

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

    // The dispatch uses the values bound before finalization.
    compareComputeResult(device, buffer, makeArray<float>(11.0f, 12.0f, 13.0f, 14.0f));
}
