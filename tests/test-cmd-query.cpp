#include "testing.h"

#include <chrono>
#include <initializer_list>
#include <cmath>

using namespace rhi;
using namespace rhi::testing;

static ComPtr<IQueryPool> createTimestampQueryPool(IDevice* device, uint32_t count)
{
    QueryPoolDesc queryPoolDesc = {};
    queryPoolDesc.type = QueryType::Timestamp;
    queryPoolDesc.count = count;

    ComPtr<IQueryPool> queryPool;
    REQUIRE_CALL(device->createQueryPool(queryPoolDesc, queryPool.writeRef()));
    return queryPool;
}

static ComPtr<ICommandBuffer> createTimestampCommandBuffer(
    ICommandQueue* queue,
    IQueryPool* queryPool,
    std::initializer_list<uint32_t> queryIndices
)
{
    auto commandEncoder = queue->createCommandEncoder();
    for (uint32_t queryIndex : queryIndices)
    {
        commandEncoder->writeTimestamp(queryPool, queryIndex);
    }
    return commandEncoder->finish();
}

static void checkQueryResultReady(IQueryPool* queryPool, uint32_t queryIndex, uint32_t count)
{
    QueryResultState state = QueryResultState::Reset;
    REQUIRE_CALL(queryPool->getResultState(queryIndex, count, &state));
    CHECK(state == QueryResultState::Resolved);
}

static void checkQueryResultReset(IQueryPool* queryPool, uint32_t queryIndex, uint32_t count)
{
    QueryResultState state = QueryResultState::Resolved;
    REQUIRE_CALL(queryPool->getResultState(queryIndex, count, &state));
    CHECK(state == QueryResultState::Reset);
}

struct AccelerationStructureQueryBuild
{
    ComPtr<IQueryPool> queryPool;
    ComPtr<ICommandBuffer> commandBuffer;
};

static AccelerationStructureQueryBuild createAccelerationStructureCompactionQueryBuild(
    IDevice* device,
    ICommandQueue* queue
)
{
    struct Vertex
    {
        float position[3];
    };

    const Vertex vertices[] = {
        {{0.0f, 0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}},
    };
    const uint32_t indices[] = {0, 1, 2};

    BufferDesc vertexBufferDesc = {};
    vertexBufferDesc.size = sizeof(vertices);
    vertexBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    vertexBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> vertexBuffer = device->createBuffer(vertexBufferDesc, vertices);
    REQUIRE(vertexBuffer != nullptr);

    BufferDesc indexBufferDesc = {};
    indexBufferDesc.size = sizeof(indices);
    indexBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    indexBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> indexBuffer = device->createBuffer(indexBufferDesc, indices);
    REQUIRE(indexBuffer != nullptr);

    AccelerationStructureBuildInput buildInput = {};
    buildInput.type = AccelerationStructureBuildInputType::Triangles;
    buildInput.triangles.vertexBuffers[0] = vertexBuffer;
    buildInput.triangles.vertexBufferCount = 1;
    buildInput.triangles.vertexFormat = Format::RGB32Float;
    buildInput.triangles.vertexCount = SLANG_COUNT_OF(vertices);
    buildInput.triangles.vertexStride = sizeof(Vertex);
    buildInput.triangles.indexBuffer = indexBuffer;
    buildInput.triangles.indexFormat = IndexFormat::Uint32;
    buildInput.triangles.indexCount = SLANG_COUNT_OF(indices);
    buildInput.triangles.flags = AccelerationStructureGeometryFlags::Opaque;

    AccelerationStructureBuildDesc buildDesc = {};
    buildDesc.inputs = &buildInput;
    buildDesc.inputCount = 1;
    buildDesc.flags = AccelerationStructureBuildFlags::AllowCompaction;

    AccelerationStructureSizes sizes = {};
    REQUIRE_CALL(device->getAccelerationStructureSizes(buildDesc, &sizes));

    BufferDesc scratchBufferDesc = {};
    scratchBufferDesc.usage = BufferUsage::UnorderedAccess;
    scratchBufferDesc.defaultState = ResourceState::UnorderedAccess;
    scratchBufferDesc.size = sizes.scratchSize;
    ComPtr<IBuffer> scratchBuffer = device->createBuffer(scratchBufferDesc);
    REQUIRE(scratchBuffer != nullptr);

    AccelerationStructureDesc draftDesc = {};
    draftDesc.kind = AccelerationStructureKind::BottomLevel;
    draftDesc.size = sizes.accelerationStructureSize;
    ComPtr<IAccelerationStructure> draftAS;
    REQUIRE_CALL(device->createAccelerationStructure(draftDesc, draftAS.writeRef()));

    QueryPoolDesc queryPoolDesc = {};
    queryPoolDesc.type = QueryType::AccelerationStructureCompactedSize;
    queryPoolDesc.count = 1;
    ComPtr<IQueryPool> queryPool;
    REQUIRE_CALL(device->createQueryPool(queryPoolDesc, queryPool.writeRef()));

    AccelerationStructureQueryDesc queryDesc = {};
    queryDesc.queryType = QueryType::AccelerationStructureCompactedSize;
    queryDesc.queryPool = queryPool;

    auto commandEncoder = queue->createCommandEncoder();
    commandEncoder->buildAccelerationStructure(buildDesc, draftAS, nullptr, scratchBuffer, 1, &queryDesc);

    return {queryPool, commandEncoder->finish()};
}

GPU_TEST_CASE("cmd-query-resolve-host", ALL)
{
    if (!device->hasFeature(Feature::TimestampQuery))
    {
        SKIP("Timestamp queries not supported");
    }

    uint64_t timestampFrequency = device->getInfo().timestampFrequency;
    CHECK(timestampFrequency > 0);

    const uint32_t queryCount = 16;
    auto queryPool = createTimestampQueryPool(device, queryCount);

    const uint32_t N = 16;
    std::vector<uint64_t> results(N * 2);

    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

    for (uint32_t i = 0; i < N; ++i)
    {
        uint32_t queryIndex = (i % (queryCount - 2));
        {
            auto queue = device->getQueue(QueueType::Graphics);
            REQUIRE_CALL(queue->submit(createTimestampCommandBuffer(queue, queryPool, {queryIndex, queryIndex + 1})));
            REQUIRE_CALL(queue->waitOnHost());
        }

        REQUIRE_CALL(queryPool->getResult(queryIndex, 2, &results[i * 2]));
        REQUIRE_CALL(queryPool->reset());
    }

    std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
    auto durationCPU = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000000.0;

    double minTime = std::numeric_limits<double>::max();
    double maxTime = 0.0;

    for (uint32_t i = 0; i < N * 2; ++i)
    {
        double time = static_cast<double>(results[i]) / timestampFrequency;
        minTime = std::min(minTime, time);
        maxTime = std::max(maxTime, time);
        if (i > 0)
        {
            CHECK(results[i] >= results[i - 1]);
        }
    }

    double durationGPU = maxTime - minTime;
    // durationCPU is intended to bound the GPU timestamp span, but it is
    // truncated to whole microseconds above and a fast loop spans only a few
    // microseconds, so the containment margin can vanish. Allow for truncation
    // and cross-clock jitter (~2us) plus the GPU's own timestamp granularity
    // (2 / timestampFrequency, which dominates on coarse-timer backends) so this
    // stays a units sanity-check rather than a sub-microsecond race.
    CHECK(durationGPU <= durationCPU + 2e-6 + 2.0 / static_cast<double>(timestampFrequency));
    // printf("Duration CPU: %.3f ms, GPU: %.3f\n", durationCPU * 1000.0, durationGPU * 1000.0);
}

GPU_TEST_CASE("cmd-query-zero-count-range", ALL)
{
    if (!device->hasFeature(Feature::TimestampQuery))
    {
        SKIP("Timestamp queries not supported");
    }

    auto queryPool = createTimestampQueryPool(device, 1);

    checkQueryResultReady(queryPool, 0, 0);
    checkQueryResultReady(queryPool, 1, 0);
    uint64_t emptyResult = 0;
    CHECK(queryPool->getResult(0, 0, &emptyResult) == SLANG_OK);
    CHECK(queryPool->getResult(1, 0, &emptyResult) == SLANG_OK);
    CHECK(queryPool->reset(0, 0) == SLANG_OK);
    CHECK(queryPool->reset(1, 0) == SLANG_OK);
}

GPU_TEST_CASE("cmd-query-result-readiness", ALL)
{
    if (!device->hasFeature(Feature::TimestampQuery))
    {
        SKIP("Timestamp queries not supported");
    }

    auto queryPool = createTimestampQueryPool(device, 1);
    auto queue = device->getQueue(QueueType::Graphics);

    checkQueryResultReset(queryPool, 0, 1);

    auto commandBuffer = createTimestampCommandBuffer(queue, queryPool, {0});
    checkQueryResultReset(queryPool, 0, 1);

    REQUIRE_CALL(queryPool->reset());
    checkQueryResultReset(queryPool, 0, 1);

    REQUIRE_CALL(queue->submit(commandBuffer));

    QueryResultState state = QueryResultState::Reset;
    REQUIRE_CALL(queryPool->getResultState(0, 1, &state));
    CHECK((state == QueryResultState::Pending || state == QueryResultState::Resolved));

    REQUIRE_CALL(queue->waitOnHost());
    checkQueryResultReady(queryPool, 0, 1);

    uint64_t timestamp = 0;
    REQUIRE_CALL(queryPool->getResult(0, 1, &timestamp));
    REQUIRE_CALL(queryPool->reset());
    checkQueryResultReset(queryPool, 0, 1);
}

GPU_TEST_CASE("cmd-query-partial-range-readiness", ALL)
{
    if (!device->hasFeature(Feature::TimestampQuery))
    {
        SKIP("Timestamp queries not supported");
    }

    auto queryPool = createTimestampQueryPool(device, 2);
    auto queue = device->getQueue(QueueType::Graphics);

    REQUIRE_CALL(queue->submit(createTimestampCommandBuffer(queue, queryPool, {0})));
    REQUIRE_CALL(queue->waitOnHost());

    uint64_t timestamp = 0;
    checkQueryResultReady(queryPool, 0, 1);
    REQUIRE_CALL(queryPool->getResult(0, 1, &timestamp));

    checkQueryResultReset(queryPool, 1, 1);
    checkQueryResultReset(queryPool, 0, 2);
    CHECK(queryPool->getResult(0, 2, &timestamp) == SLANG_FAIL);
}

GPU_TEST_CASE("cmd-query-ready-result-ranges", ALL)
{
    if (!device->hasFeature(Feature::TimestampQuery))
    {
        SKIP("Timestamp queries not supported");
    }

    auto queryPool = createTimestampQueryPool(device, 3);
    auto queue = device->getQueue(QueueType::Graphics);

    REQUIRE_CALL(queue->submit(createTimestampCommandBuffer(queue, queryPool, {0, 1, 2})));
    REQUIRE_CALL(queue->waitOnHost());

    uint64_t timestamps[3] = {};
    checkQueryResultReady(queryPool, 0, 3);
    checkQueryResultReady(queryPool, 1, 1);
    checkQueryResultReady(queryPool, 1, 2);
    REQUIRE_CALL(queryPool->getResult(0, 3, timestamps));

    CHECK(timestamps[1] >= timestamps[0]);
    CHECK(timestamps[2] >= timestamps[1]);
}

GPU_TEST_CASE("cmd-query-reuse-slot-without-reset", ALL)
{
    if (!device->hasFeature(Feature::TimestampQuery))
    {
        SKIP("Timestamp queries not supported");
    }

    auto queryPool = createTimestampQueryPool(device, 1);
    auto queue = device->getQueue(QueueType::Graphics);

    REQUIRE_CALL(queue->submit(createTimestampCommandBuffer(queue, queryPool, {0})));
    REQUIRE_CALL(queue->waitOnHost());

    uint64_t firstTimestamp = 0;
    checkQueryResultReady(queryPool, 0, 1);
    REQUIRE_CALL(queryPool->getResult(0, 1, &firstTimestamp));

    REQUIRE_CALL(queue->submit(createTimestampCommandBuffer(queue, queryPool, {0})));
    REQUIRE_CALL(queue->waitOnHost());

    uint64_t secondTimestamp = 0;
    checkQueryResultReady(queryPool, 0, 1);
    REQUIRE_CALL(queryPool->getResult(0, 1, &secondTimestamp));
    CHECK(secondTimestamp >= firstTimestamp);
}

GPU_TEST_CASE("cmd-query-reset-invalidates-ready-result", ALL)
{
    if (!device->hasFeature(Feature::TimestampQuery))
    {
        SKIP("Timestamp queries not supported");
    }

    auto queryPool = createTimestampQueryPool(device, 1);
    auto queue = device->getQueue(QueueType::Graphics);

    REQUIRE_CALL(queue->submit(createTimestampCommandBuffer(queue, queryPool, {0})));
    REQUIRE_CALL(queue->waitOnHost());

    uint64_t timestamp = 0;
    checkQueryResultReady(queryPool, 0, 1);
    REQUIRE_CALL(queryPool->getResult(0, 1, &timestamp));

    REQUIRE_CALL(queryPool->reset());
    checkQueryResultReset(queryPool, 0, 1);
    CHECK(queryPool->getResult(0, 1, &timestamp) == SLANG_FAIL);
}

GPU_TEST_CASE("cmd-query-reset-range", ALL)
{
    if (!device->hasFeature(Feature::TimestampQuery))
    {
        SKIP("Timestamp queries not supported");
    }

    auto queryPool = createTimestampQueryPool(device, 3);
    auto queue = device->getQueue(QueueType::Graphics);

    REQUIRE_CALL(queue->submit(createTimestampCommandBuffer(queue, queryPool, {0, 1, 2})));
    REQUIRE_CALL(queue->waitOnHost());
    checkQueryResultReady(queryPool, 0, 3);

    REQUIRE_CALL(queryPool->reset(1, 1));
    checkQueryResultReady(queryPool, 0, 1);
    checkQueryResultReset(queryPool, 1, 1);
    checkQueryResultReady(queryPool, 2, 1);
    checkQueryResultReset(queryPool, 0, 3);

    uint64_t timestamp = 0;
    REQUIRE_CALL(queryPool->getResult(0, 1, &timestamp));
    REQUIRE_CALL(queryPool->getResult(2, 1, &timestamp));
    CHECK(queryPool->getResult(1, 1, &timestamp) == SLANG_FAIL);

    REQUIRE_CALL(queue->submit(createTimestampCommandBuffer(queue, queryPool, {1})));
    REQUIRE_CALL(queue->waitOnHost());
    checkQueryResultReady(queryPool, 0, 3);

    uint64_t timestamps[3] = {};
    REQUIRE_CALL(queryPool->getResult(0, 3, timestamps));
}

GPU_TEST_CASE("cmd-query-get-result-without-wait", ALL)
{
    if (!device->hasFeature(Feature::TimestampQuery))
    {
        SKIP("Timestamp queries not supported");
    }

    auto queryPool = createTimestampQueryPool(device, 1);
    auto queue = device->getQueue(QueueType::Graphics);

    REQUIRE_CALL(queue->submit(createTimestampCommandBuffer(queue, queryPool, {0})));

    uint64_t timestamp = 0;
    REQUIRE_CALL(queryPool->getResult(0, 1, &timestamp));
    checkQueryResultReady(queryPool, 0, 1);
}

GPU_TEST_CASE("cmd-query-acceleration-structure-get-result-without-wait", D3D12 | Vulkan | CUDA)
{
    if (!device->hasFeature(Feature::AccelerationStructure))
    {
        SKIP("Acceleration structures not supported");
    }

    auto queue = device->getQueue(QueueType::Graphics);
    auto build = createAccelerationStructureCompactionQueryBuild(device, queue);

    checkQueryResultReset(build.queryPool, 0, 1);
    REQUIRE_CALL(queue->submit(build.commandBuffer));

    uint64_t compactedSize = 0;
    REQUIRE_CALL(build.queryPool->getResult(0, 1, &compactedSize));
    CHECK(compactedSize > 0);
    checkQueryResultReady(build.queryPool, 0, 1);

    REQUIRE_CALL(build.queryPool->reset(0, 1));
    checkQueryResultReset(build.queryPool, 0, 1);
}

GPU_TEST_CASE("cmd-query-range-across-command-buffers", ALL)
{
    if (!device->hasFeature(Feature::TimestampQuery))
    {
        SKIP("Timestamp queries not supported");
    }

    auto queryPool = createTimestampQueryPool(device, 2);
    auto queue = device->getQueue(QueueType::Graphics);

    ComPtr<ICommandBuffer> commandBuffers[2] = {
        createTimestampCommandBuffer(queue, queryPool, {0}),
        createTimestampCommandBuffer(queue, queryPool, {1}),
    };
    ICommandBuffer* commandBufferPtrs[2] = {commandBuffers[0], commandBuffers[1]};

    SubmitDesc submitDesc = {};
    submitDesc.commandBufferCount = 2;
    submitDesc.commandBuffers = commandBufferPtrs;
    REQUIRE_CALL(queue->submit(submitDesc));
    REQUIRE_CALL(queue->waitOnHost());

    uint64_t timestamps[2] = {};
    checkQueryResultReady(queryPool, 0, 2);
    REQUIRE_CALL(queryPool->getResult(0, 2, timestamps));
    CHECK(timestamps[1] >= timestamps[0]);
}

GPU_TEST_CASE("cmd-query-range-across-submissions", ALL)
{
    if (!device->hasFeature(Feature::TimestampQuery))
    {
        SKIP("Timestamp queries not supported");
    }

    auto queryPool = createTimestampQueryPool(device, 2);
    auto queue = device->getQueue(QueueType::Graphics);

    REQUIRE_CALL(queue->submit(createTimestampCommandBuffer(queue, queryPool, {0})));
    REQUIRE_CALL(queue->waitOnHost());
    checkQueryResultReady(queryPool, 0, 1);
    checkQueryResultReset(queryPool, 0, 2);

    REQUIRE_CALL(queue->submit(createTimestampCommandBuffer(queue, queryPool, {1})));
    REQUIRE_CALL(queue->waitOnHost());

    uint64_t timestamps[2] = {};
    checkQueryResultReady(queryPool, 0, 2);
    REQUIRE_CALL(queryPool->getResult(0, 2, timestamps));
    CHECK(timestamps[1] >= timestamps[0]);
}

GPU_TEST_CASE("cmd-query-d3d12-get-result-requires-submission", D3D12)
{
    if (!device->hasFeature(Feature::TimestampQuery))
    {
        SKIP("Timestamp queries not supported");
    }

    auto queryPool = createTimestampQueryPool(device, 1);
    auto queue = device->getQueue(QueueType::Graphics);
    auto commandBuffer = createTimestampCommandBuffer(queue, queryPool, {0});

    uint64_t timestamp = 0;
    checkQueryResultReset(queryPool, 0, 1);
    CHECK(queryPool->getResult(0, 1, &timestamp) == SLANG_FAIL);

    REQUIRE_CALL(queue->submit(commandBuffer));
    REQUIRE_CALL(queue->waitOnHost());
    REQUIRE_CALL(queryPool->getResult(0, 1, &timestamp));
}

GPU_TEST_CASE("cmd-query-resolve-device", ALL & ~(D3D11 | CPU | CUDA))
{
    if (!device->hasFeature(Feature::TimestampQuery))
    {
        SKIP("Timestamp queries not supported");
    }

    uint64_t timestampFrequency = device->getInfo().timestampFrequency;
    CHECK(timestampFrequency > 0);

    const uint32_t queryCount = 16;
    auto queryPool = createTimestampQueryPool(device, queryCount);

    const uint32_t N = 16;
    std::vector<uint64_t> results(N * 2);

    BufferDesc bufferDesc = {};
    bufferDesc.size = N * 2 * sizeof(uint64_t);
    bufferDesc.usage = BufferUsage::CopyDestination | BufferUsage::CopySource;
    ComPtr<IBuffer> resultBuffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, resultBuffer.writeRef()));

    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

    for (uint32_t i = 0; i < N; ++i)
    {
        uint32_t queryIndex = (i % (queryCount - 2));
        {
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            commandEncoder->writeTimestamp(queryPool, queryIndex);
            commandEncoder->writeTimestamp(queryPool, queryIndex + 1);
            commandEncoder->resolveQuery(queryPool, queryIndex, 2, resultBuffer, i * 2 * sizeof(uint64_t));

            REQUIRE_CALL(queue->submit(commandEncoder->finish()));
            REQUIRE_CALL(queue->waitOnHost());
        }

        REQUIRE_CALL(queryPool->reset());
    }

    std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
    auto durationCPU = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000000.0;

    REQUIRE_CALL(device->readBuffer(resultBuffer, 0, N * 2 * sizeof(uint64_t), results.data()));

    double minTime = std::numeric_limits<double>::max();
    double maxTime = 0.0;

    for (uint32_t i = 0; i < N * 2; ++i)
    {
        double time = static_cast<double>(results[i]) / timestampFrequency;
        minTime = std::min(minTime, time);
        maxTime = std::max(maxTime, time);
        if (i > 0)
        {
            CHECK(results[i] >= results[i - 1]);
        }
    }

    double durationGPU = maxTime - minTime;
    // durationCPU is intended to bound the GPU timestamp span, but it is
    // truncated to whole microseconds above and a fast loop spans only a few
    // microseconds, so the containment margin can vanish. Allow for truncation
    // and cross-clock jitter (~2us) plus the GPU's own timestamp granularity
    // (2 / timestampFrequency, which dominates on coarse-timer backends) so this
    // stays a units sanity-check rather than a sub-microsecond race.
    CHECK(durationGPU <= durationCPU + 2e-6 + 2.0 / static_cast<double>(timestampFrequency));
    // printf("Duration CPU: %.3f ms, GPU: %.3f\n", durationCPU * 1000.0, durationGPU * 1000.0);
}

static uint64_t getGpuToleranceTicks(const TimestampCalibration& calibration)
{
    return uint64_t(
               std::ceil(
                   (long double)calibration.maxDeviationNs * (long double)calibration.gpuFrequency /
                   (long double)1000000000.0
               )
           ) +
           1;
}

GPU_TEST_CASE("cmd-query-timestamp-calibration", ALL)
{
    if (!device->hasFeature(Feature::TimestampCalibration))
    {
        SKIP("Timestamp calibration not supported");
    }

    REQUIRE(device->hasFeature(Feature::TimestampQuery));

    auto queue = device->getQueue(QueueType::Graphics);
    REQUIRE(queue);

    TimestampCalibration calibration0 = {};
    TimestampCalibration calibration1 = {};
    REQUIRE_CALL(queue->getTimestampCalibration(&calibration0));
    REQUIRE_CALL(queue->getTimestampCalibration(&calibration1));

    CHECK(calibration0.cpuDomain != CpuTimestampDomain::Unknown);
    CHECK(calibration0.cpuFrequency > 0);
    CHECK(calibration0.gpuFrequency > 0);
    CHECK(calibration1.cpuDomain == calibration0.cpuDomain);
    CHECK(calibration1.cpuFrequency == calibration0.cpuFrequency);
    CHECK(calibration1.gpuFrequency == calibration0.gpuFrequency);
    CHECK(calibration1.cpuTimestamp >= calibration0.cpuTimestamp);
    CHECK(calibration1.gpuTimestamp >= calibration0.gpuTimestamp);

    QueryPoolDesc queryPoolDesc = {};
    queryPoolDesc.type = QueryType::Timestamp;
    queryPoolDesc.count = 1;
    ComPtr<IQueryPool> queryPool;
    REQUIRE_CALL(device->createQueryPool(queryPoolDesc, queryPool.writeRef()));

    TimestampCalibration before = {};
    TimestampCalibration after = {};
    REQUIRE_CALL(queue->getTimestampCalibration(&before));

    auto commandEncoder = queue->createCommandEncoder();
    commandEncoder->writeTimestamp(queryPool, 0);
    REQUIRE_CALL(queue->submit(commandEncoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());

    REQUIRE_CALL(queue->getTimestampCalibration(&after));

    uint64_t timestamp = 0;
    REQUIRE_CALL(queryPool->getResult(0, 1, &timestamp));

    const uint64_t lowerTolerance = getGpuToleranceTicks(before);
    const uint64_t upperTolerance = getGpuToleranceTicks(after);
    const uint64_t lowerBound = before.gpuTimestamp > lowerTolerance ? before.gpuTimestamp - lowerTolerance : 0;
    const uint64_t upperBound = after.gpuTimestamp > std::numeric_limits<uint64_t>::max() - upperTolerance
                                    ? std::numeric_limits<uint64_t>::max()
                                    : after.gpuTimestamp + upperTolerance;

    CHECK(timestamp >= lowerBound);
    CHECK(timestamp <= upperBound);
}
