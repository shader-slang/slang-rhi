#include "testing.h"

#include <chrono>

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("cmd-query-resolve-host", ALL)
{
    if (!device->hasFeature(Feature::TimestampQuery))
    {
        SKIP("Timestamp queries not supported");
    }

    uint64_t timestampFrequency = device->getInfo().timestampFrequency;
    CHECK(timestampFrequency > 0);

    QueryPoolDesc queryPoolDesc;
    queryPoolDesc.type = QueryType::Timestamp;
    queryPoolDesc.count = 16;
    ComPtr<IQueryPool> queryPool;
    REQUIRE_CALL(device->createQueryPool(queryPoolDesc, queryPool.writeRef()));

    const uint32_t N = 16;
    std::vector<uint64_t> results(N * 2);

    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

    for (uint32_t i = 0; i < N; ++i)
    {
        uint32_t queryIndex = (i % (queryPoolDesc.count - 2));
        {
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            commandEncoder->writeTimestamp(queryPool, queryIndex);
            commandEncoder->writeTimestamp(queryPool, queryIndex + 1);

            queue->submit(commandEncoder->finish());
            queue->waitOnHost();
        }

        queryPool->getResult(queryIndex, 2, &results[i * 2]);
        queryPool->reset();
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
    CHECK(durationGPU < durationCPU);
    // printf("Duration CPU: %.3f ms, GPU: %.3f\n", durationCPU * 1000.0, durationGPU * 1000.0);
}

GPU_TEST_CASE("cmd-query-resolve-device", ALL & ~(D3D11 | CPU | CUDA))
{
    if (!device->hasFeature(Feature::TimestampQuery))
    {
        SKIP("Timestamp queries not supported");
    }

    uint64_t timestampFrequency = device->getInfo().timestampFrequency;
    CHECK(timestampFrequency > 0);

    QueryPoolDesc queryPoolDesc = {};
    queryPoolDesc.type = QueryType::Timestamp;
    queryPoolDesc.count = 16;
    ComPtr<IQueryPool> queryPool;
    REQUIRE_CALL(device->createQueryPool(queryPoolDesc, queryPool.writeRef()));

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
        uint32_t queryIndex = (i % (queryPoolDesc.count - 2));
        {
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            commandEncoder->writeTimestamp(queryPool, queryIndex);
            commandEncoder->writeTimestamp(queryPool, queryIndex + 1);
            commandEncoder->resolveQuery(queryPool, queryIndex, 2, resultBuffer, i * 2 * sizeof(uint64_t));

            queue->submit(commandEncoder->finish());
            queue->waitOnHost();
        }

        queryPool->reset();
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
    CHECK(durationGPU < durationCPU);
    // printf("Duration CPU: %.3f ms, GPU: %.3f\n", durationCPU * 1000.0, durationGPU * 1000.0);
}
