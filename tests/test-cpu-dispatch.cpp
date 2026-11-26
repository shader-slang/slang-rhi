#include "testing.h"

#include <thread>
#include <vector>

using namespace rhi;
using namespace rhi::testing;

// The CPU device splits a dispatch into slices of thread groups that run on the global task pool.

static ComPtr<IBuffer> createUintBuffer(IDevice* device, size_t count)
{
    std::vector<uint32_t> zeros(count, 0);
    BufferDesc bufferDesc = {};
    bufferDesc.size = count * sizeof(uint32_t);
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, zeros.data(), buffer.writeRef()));
    return buffer;
}

// Every group of the dispatch runs exactly once, whichever axis the dispatch is split along.
GPU_TEST_CASE("cpu-dispatch-every-group-once", CPU)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-cpu-dispatch", "writeIds", shaderProgram.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const uint32_t groupSize[3] = {4, 2, 1};
    const uint32_t dispatches[][3] = {
        {1, 1, 1},    // a single group
        {2, 1, 1},    // two slices: the submitting thread and one pool task
        {1000, 1, 1}, // more slices than threads, split along x
        {3, 257, 2},  // split along y
        {2, 3, 131},  // split along z
        {7, 5, 3},    // split along x, with full y and z ranges in every slice
    };
    for (const auto& groups : dispatches)
    {
        CAPTURE(groups[0]);
        CAPTURE(groups[1]);
        CAPTURE(groups[2]);
        uint32_t size[3] = {groups[0] * groupSize[0], groups[1] * groupSize[1], groups[2] * groupSize[2]};
        size_t threadTotal = size_t(size[0]) * size[1] * size[2];
        ComPtr<IBuffer> ids = createUintBuffer(device, threadTotal);
        ComPtr<IBuffer> threadCount = createUintBuffer(device, 1);

        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject);
        cursor["ids"].setBinding(ids);
        cursor["threadCount"].setBinding(threadCount);
        cursor["dispatchSize"].setData(size, sizeof(size));
        passEncoder->dispatchCompute(groups[0], groups[1], groups[2]);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();

        std::vector<uint32_t> expected(threadTotal);
        for (size_t i = 0; i < threadTotal; ++i)
            expected[i] = uint32_t(i + 1);
        compareComputeResult(device, ids, std::span<uint32_t>(expected));
        compareComputeResult(device, threadCount, makeArray<uint32_t>(uint32_t(threadTotal)));
    }
}

// The groups of one dispatch run concurrently: each group waits until all groups have started,
// which cannot happen when they run one after another.
GPU_TEST_CASE("cpu-dispatch-groups-run-concurrently", CPU)
{
    if (std::thread::hardware_concurrency() < 2)
        SKIP("needs at least two hardware threads");

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-cpu-dispatch", "waitForAllGroups", shaderProgram.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const uint32_t groupCount = 2;
    ComPtr<IBuffer> arrived = createUintBuffer(device, 1);
    ComPtr<IBuffer> sawAll = createUintBuffer(device, groupCount);

    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();
    auto passEncoder = commandEncoder->beginComputePass();
    auto rootObject = passEncoder->bindPipeline(pipeline);
    ShaderCursor cursor(rootObject);
    cursor["arrived"].setBinding(arrived);
    cursor["sawAll"].setBinding(sawAll);
    cursor["groupCount"].setData(groupCount);
    // Each poll is an atomic read-modify-write: 100 M polls is far longer than a task pool
    // takes to start a second slice, and still ends the test quickly when the groups run
    // serially.
    cursor["spinBudget"].setData(uint32_t(100000000));
    passEncoder->dispatchCompute(groupCount, 1, 1);
    passEncoder->end();
    queue->submit(commandEncoder->finish());
    queue->waitOnHost();

    compareComputeResult(device, sawAll, makeArray<uint32_t>(1u, 1u));
}
