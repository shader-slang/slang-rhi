#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

// Test that a work graph pipeline can be created and that the memory
// requirements are internally consistent (minSize > 0, minSize <= maxSize).
GPU_TEST_CASE("work-graph-pipeline-creation", ALL)
{
    SKIP("disabled until Slang work graph shader support is merged");

    if (!device->hasFeature(Feature::WorkGraph))
        SKIP("work graphs not supported");

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-work-graph-trivial", "writeNode", shaderProgram.writeRef()));

    WorkGraphPipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IWorkGraphPipeline> pipeline;
    REQUIRE_CALL(device->createWorkGraphPipeline(pipelineDesc, pipeline.writeRef()));

    WorkGraphMemoryRequirements memReqs = {};
    REQUIRE_CALL(pipeline->getWorkGraphMemoryRequirements(&memReqs));
    CHECK(memReqs.minSizeInBytes > 0);
    CHECK(memReqs.minSizeInBytes <= memReqs.maxSizeInBytes);
}

// Dispatch a work graph with one CPU input record and verify the node writes
// the expected value into a UAV output buffer.
GPU_TEST_CASE("work-graph-trivial", ALL)
{
    SKIP("disabled until Slang work graph shader support is merged");

    if (!device->hasFeature(Feature::WorkGraph))
        SKIP("work graphs not supported");

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-work-graph-trivial", "writeNode", shaderProgram.writeRef()));

    WorkGraphPipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IWorkGraphPipeline> pipeline;
    REQUIRE_CALL(device->createWorkGraphPipeline(pipelineDesc, pipeline.writeRef()));

    WorkGraphMemoryRequirements memReqs = {};
    REQUIRE_CALL(pipeline->getWorkGraphMemoryRequirements(&memReqs));

    // Backing store required by the work graph runtime.
    BufferDesc backingStoreDesc = {};
    backingStoreDesc.size = memReqs.minSizeInBytes;
    backingStoreDesc.usage = BufferUsage::UnorderedAccess;
    backingStoreDesc.defaultState = ResourceState::UnorderedAccess;
    backingStoreDesc.memoryType = MemoryType::DeviceLocal;
    ComPtr<IBuffer> backingStore;
    REQUIRE_CALL(device->createBuffer(backingStoreDesc, nullptr, backingStore.writeRef()));

    // Output buffer — initially zero, the node will write kOutputValue into [0].
    static const uint32_t kOutputValue = 42u;
    uint32_t initialData = 0u;
    BufferDesc outputDesc = {};
    outputDesc.size = sizeof(uint32_t);
    outputDesc.elementSize = sizeof(uint32_t);
    outputDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess |
                       BufferUsage::CopyDestination | BufferUsage::CopySource;
    outputDesc.defaultState = ResourceState::UnorderedAccess;
    outputDesc.memoryType = MemoryType::DeviceLocal;
    ComPtr<IBuffer> outputBuffer;
    REQUIRE_CALL(device->createBuffer(outputDesc, &initialData, outputBuffer.writeRef()));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginWorkGraphPass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor(rootObject)["outputBuffer"].setBinding(outputBuffer);

        struct NodeRecord
        {
            uint32_t value;
        };
        NodeRecord record = {kOutputValue};
        passEncoder->dispatchGraph(backingStore, /*entryPointIndex=*/0, /*numRecords=*/1, &record, sizeof(NodeRecord));
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, outputBuffer, makeArray<uint32_t>(kOutputValue));
}
