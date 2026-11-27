#include "testing.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

using namespace rhi;
using namespace rhi::testing;

// The CPU device splits a large dispatch into slices of thread groups that run on the global task
// pool. A slice gets at least 8192 threads, so a dispatch needs 2 x 8192 threads for two slices.

namespace {

ComPtr<IBuffer> createUintBuffer(IDevice* device, size_t count)
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

// Replaces the global task pool for one test, and restores the default pool afterwards.
// The pool can only be replaced while no device is alive.
struct ScopedTaskPool
{
    ComPtr<IDevice>& device;

    ScopedTaskPool(ComPtr<IDevice>& device, int workerCount)
        : device(device)
    {
        releaseCachedDevices();
        REQUIRE_CALL(getRHI()->initTaskPool(workerCount));
    }

    ~ScopedTaskPool()
    {
        device.setNull();
        CHECK(SLANG_SUCCEEDED(getRHI()->initTaskPool(-1)));
    }
};

// Every group of each dispatch runs exactly once, whichever axis the dispatch is split along.
void testEveryGroupOnce(IDevice* device)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-cpu-dispatch", "writeIds", shaderProgram.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    // 8 threads per group.
    const uint32_t groupSize[3] = {4, 2, 1};
    const uint32_t dispatches[][3] = {
        {1, 1, 1},    // a single group
        {2, 1, 1},    // too small to split: runs inline
        {1000, 1, 1}, // 8000 threads: still inline
        {4096, 1, 1}, // 4 slices along x
        {3, 1031, 2}, // 6 slices along y
        {2, 3, 1031}, // 6 slices along z
        {700, 5, 3},  // split along x, with full y and z ranges in every slice
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

} // namespace

// With the default pool: one worker per hardware thread.
GPU_TEST_CASE("cpu-dispatch-every-group-once", CPU)
{
    testEveryGroupOnce(device);
}

// With a single worker: the submitting thread and the worker share the slices.
GPU_TEST_CASE("cpu-dispatch-every-group-once-single-worker", CPU | DontCreateDevice)
{
    ScopedTaskPool taskPool(device, 1);
    device = createTestingDevice(ctx, ctx->deviceType, false);
    REQUIRE(device);
    testEveryGroupOnce(device);
}

// With a BlockingTaskPool, which has no workers: every dispatch runs inline.
GPU_TEST_CASE("cpu-dispatch-every-group-once-blocking-pool", CPU | DontCreateDevice)
{
    ScopedTaskPool taskPool(device, 0);
    device = createTestingDevice(ctx, ctx->deviceType, false);
    REQUIRE(device);
    testEveryGroupOnce(device);
}

// The slices of one dispatch run concurrently: the first group waits until the last group has
// started, which cannot happen when they run one after another. The pool has one worker thread,
// so the second slice always has a thread of its own, however many CPUs the process may use.
GPU_TEST_CASE("cpu-dispatch-groups-run-concurrently", CPU | DontCreateDevice)
{
    ScopedTaskPool taskPool(device, 1);
    device = createTestingDevice(ctx, ctx->deviceType, false);
    REQUIRE(device);

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-cpu-dispatch", "firstGroupWaitsForLast", shaderProgram.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    // 256 groups of 64 threads: two slices of 128 groups.
    const uint32_t groupCount = 256;
    ComPtr<IBuffer> flags = createUintBuffer(device, 2);
    ComPtr<IBuffer> sawLastGroup = createUintBuffer(device, 1);

    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();
    auto passEncoder = commandEncoder->beginComputePass();
    auto rootObject = passEncoder->bindPipeline(pipeline);
    ShaderCursor cursor(rootObject);
    cursor["flags"].setBinding(flags);
    cursor["sawLastGroup"].setBinding(sawLastGroup);
    cursor["lastGroup"].setData(groupCount - 1);
    passEncoder->dispatchCompute(groupCount, 1, 1);
    passEncoder->end();
    ComPtr<ICommandBuffer> commandBuffer = commandEncoder->finish();

    // If the groups run one after another, the first group would wait forever: after a few
    // seconds, set flags[1] so that it gives up. A CPU buffer's device address is its host memory.
    std::mutex mutex;
    std::condition_variable done;
    bool finished = false;
    std::thread watchdog(
        [&]()
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (!done.wait_for(
                    lock,
                    std::chrono::seconds(5),
                    [&]
                    {
                        return finished;
                    }
                ))
            {
                uint32_t* flagData = reinterpret_cast<uint32_t*>(flags->getDeviceAddress());
                std::atomic_ref<uint32_t>(flagData[1]).store(1);
            }
        }
    );
    queue->submit(commandBuffer);
    queue->waitOnHost();
    {
        std::lock_guard<std::mutex> lock(mutex);
        finished = true;
    }
    done.notify_one();
    watchdog.join();

    compareComputeResult(device, sawLastGroup, makeArray<uint32_t>(1u));
}
