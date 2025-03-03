#include "testing.h"

#include <string>
#include <map>
#include <functional>
#include <memory>
#include <random>

#include "rhi-shared.h"
#include "debug-layer/debug-device.h"

using namespace rhi;
using namespace rhi::testing;

Device* getSharedDevice(IDevice* device) {
    if (auto debugDevice = dynamic_cast<debug::DebugDevice*>(device))
        return (Device*)debugDevice->baseObject.get();
    else
        return (Device*)device;
}

struct UploadData
{
    std::vector<uint8_t> data;
    ComPtr<IBuffer> dst;
    Offset offset;
    Size size;

    void init(IDevice* device, Size _size, Offset _offset, int seed)
    {
        // Store size/offset.
        size = _size;
        offset = _offset;

        // Generate random data.
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> dist(0, 255);
        data.resize(size);
        for (auto& byte : data)
            byte = (uint8_t)dist(rng);

        // Create buffer big enough to contain data with offset.
        BufferDesc bufferDesc = {};
        bufferDesc.size = offset + size;
        bufferDesc.usage = BufferUsage::CopyDestination | BufferUsage::CopySource;
        REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, dst.writeRef()));
    }

    void check(IDevice* device)
    {
        // Download buffer data and validate it.
        ComPtr<ISlangBlob> blob;
        REQUIRE_CALL(device->readBuffer(dst, offset, size, blob.writeRef()));
        auto buffer_data = (uint8_t*)blob->getBufferPointer();
        auto source_data = (uint8_t*)data.data();
        CHECK_EQ(memcmp(buffer_data, source_data, size), 0);
    }
};

void testUploadToBuffer(IDevice* device, Size size, Offset offset, int tests, bool multi_encoder = false)
{
    StagingHeap& heap = getSharedDevice(device)->m_heap;
    CHECK_EQ(heap.getUsed(), 0);

    std::vector<UploadData> uploads(tests);

    for (int i = 0; i < tests; i++)
        uploads[i].init(device, size, offset, i + 42);

    {
        // Create commands to upload, either with 1 or individual encoders.
        auto queue = device->getQueue(QueueType::Graphics);
        if (!multi_encoder)
        {
            auto encoder = queue->createCommandEncoder();
            for (int i = 0; i < tests; i++)
                encoder->uploadBufferData(uploads[i].dst, uploads[i].offset, uploads[i].size, uploads[i].data.data());
            CHECK_EQ(heap.getUsed(), heap.alignUp(uploads[0].size) * tests);
            queue->submit(encoder->finish());
        }
        else
        {
            for (int i = 0; i < tests; i++)
            {
                auto encoder = queue->createCommandEncoder();
                encoder->uploadBufferData(uploads[i].dst, uploads[i].offset, uploads[i].size, uploads[i].data.data());
                CHECK_EQ(heap.getUsed(), heap.alignUp(uploads[0].size) * tests);
                queue->submit(encoder->finish());
            }
        }

        queue->waitOnHost();

        // Having waited, command buffers should be reset so heap memory should be free.
        CHECK_EQ(heap.getUsed(), 0);

        // Download buffer data and validate it.
        for (int i = 0; i < tests; i++)
            uploads[i].check(device);
    }
}

GPU_TEST_CASE("staging-heap-alloc-free", ALL)
{
    StagingHeap heap;
    heap.initialize((Device*)device);

    Size alloc_size = heap.alignUp(16);

    CHECK_EQ(heap.getUsed(), 0);
    CHECK_EQ(heap.getNumPages(), 0);

    auto allocation = heap.alloc(16, {2});
    heap.checkConsistency();

    CHECK_EQ(allocation.getOffset(), 0);
    CHECK_EQ(allocation.getSize(), alloc_size);
    CHECK_EQ(allocation.getMetaData().use, 2);
    CHECK_EQ(allocation.getPageId(), 1);
    CHECK_EQ(heap.getNumPages(), 1);
    CHECK_EQ(heap.getUsed(), alloc_size);

    auto allocation2 = heap.alloc(16, {3});
    heap.checkConsistency();

    CHECK_EQ(allocation2.getOffset(), alloc_size);
    CHECK_EQ(allocation2.getSize(), alloc_size);
    CHECK_EQ(allocation2.getMetaData().use, 3);
    CHECK_EQ(allocation2.getPageId(), 1);
    CHECK_EQ(heap.getNumPages(), 1);
    CHECK_EQ(heap.getUsed(), alloc_size * 2);

    heap.free(allocation);
    heap.checkConsistency();

    CHECK_EQ(heap.getUsed(), alloc_size);

    heap.free(allocation2);
    heap.checkConsistency();

    CHECK_EQ(heap.getUsed(), 0);
    CHECK_EQ(heap.getNumPages(), 1); // Should keep 1 empty page around
}

GPU_TEST_CASE("staging-heap-large-page", ALL)
{
    StagingHeap heap;
    heap.initialize((Device*)device);

    auto allocation = heap.alloc(16, {2});
    heap.checkConsistency();
    CHECK_EQ(allocation.getOffset(), 0);
    CHECK_EQ(allocation.getPageId(), 1);

    auto big_allocation = heap.alloc(heap.getPageSize()+1, {2});
    heap.checkConsistency();
    CHECK_EQ(big_allocation.getOffset(), 0);
    CHECK_EQ(big_allocation.getPageId(), 2);

    auto allocation2 = heap.alloc(16, {2});
    heap.checkConsistency();
    CHECK_EQ(allocation2.getOffset(), heap.getAlignment());
    CHECK_EQ(allocation2.getPageId(), 1);

    auto big_allocation2 = heap.alloc(heap.getPageSize() + 1, {2});
    heap.checkConsistency();
    CHECK_EQ(big_allocation2.getOffset(), 0);
    CHECK_EQ(big_allocation2.getPageId(), 3);

    auto allocation3 = heap.alloc(16, {2});
    heap.checkConsistency();
    CHECK_EQ(allocation3.getOffset(), heap.getAlignment()*2);
    CHECK_EQ(allocation3.getPageId(), 1);
}

GPU_TEST_CASE("staging-heap-realloc", ALL)
{
    StagingHeap heap;
    heap.initialize((Device*)device);

    Size alloc_size = heap.getPageSize() / 16;

    // Allocate a page's worth of memory in 16 chunks.
    std::vector<StagingHeap::Allocation> allocations;
    for (Size i = 0; i < 16; i++)
    {
        auto allocation = heap.alloc(alloc_size, {(int)i});
        heap.checkConsistency();
        CHECK_EQ(allocation.getOffset(), i*alloc_size);
        CHECK_EQ(allocation.getPageId(), 1);
        allocations.push_back(allocation);
    }

    // Free chunks 3 and 4.
    heap.free(allocations[3]);
    heap.checkConsistency();
    heap.free(allocations[4]);
    heap.checkConsistency();

    // Make a new allocation that should reuse the free space.
    auto allocation = heap.alloc(alloc_size * 2, {2});
    heap.checkConsistency();
    CHECK_EQ(allocation.getOffset(), 3 * alloc_size);
    CHECK_EQ(allocation.getPageId(), 1);
}

GPU_TEST_CASE("staging-heap-handles", ALL)
{
    StagingHeap heap;
    heap.initialize((Device*)device);

    // Make an allocation using ref counted handle within a scope.
    {
        auto handle = heap.allocHandle(16, {2});
        heap.checkConsistency();
        CHECK_EQ(handle->getOffset(), 0);
        CHECK_EQ(handle->getPageId(), 1);
        CHECK_EQ(heap.getUsed(), heap.getAlignment());
    }

    // Allocation should be freed when handle goes out of scope.
    CHECK_EQ(heap.getUsed(), 0);
}

GPU_TEST_CASE("cmd-upload-buffer-small", ALL)
{
    testUploadToBuffer(device, 16, 0, 1);
}

GPU_TEST_CASE("cmd-upload-buffer-big", ALL)
{
    testUploadToBuffer(device, 32 * 1024 * 1024, 0, 1);
}

GPU_TEST_CASE("cmd-upload-buffer-offset", ALL)
{
    testUploadToBuffer(device, 2048, 128, 1);
}

GPU_TEST_CASE("cmd-upload-buffer-multi", ALL)
{
    testUploadToBuffer(device, 16, 0, 30);
}

GPU_TEST_CASE("cmd-upload-buffer-multienc", ALL)
{
    testUploadToBuffer(device, 16, 0, 30);
}
