#include "testing.h"

#include <string>
#include <map>
#include <functional>
#include <memory>

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

void testUploadToBuffer(IDevice* device)
{
    StagingHeap& heap = getSharedDevice(device)->m_heap;
    CHECK_EQ(heap.getUsed(), 0);

    uint8_t srcData[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF};

    BufferDesc bufferDesc = {};
    bufferDesc.size = 16;
    bufferDesc.usage = BufferUsage::CopyDestination | BufferUsage::CopySource;

    ComPtr<IBuffer> dst;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, dst.writeRef()));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        encoder->uploadBufferData(dst, 0, sizeof(srcData), srcData);

        // Having requested upload, a chunk of heap should be allocated for the memory.
        CHECK_EQ(heap.getUsed(), heap.alignUp(sizeof(srcData)));

        // Submit+wait.
        queue->submit(encoder->finish());
        queue->waitOnHost();

        // Having waited, command buffers should be reset so heap memory should be free.
        CHECK_EQ(heap.getUsed(), 0);

        ComPtr<ISlangBlob> blob;
        REQUIRE_CALL(device->readBuffer(dst, 0, 16, blob.writeRef()));
        auto data = (uint8_t*)blob->getBufferPointer();
        for (int i = 0; i < 16; i++)
        {
            CHECK_EQ(data[i], srcData[i]);
        }
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

GPU_TEST_CASE("cmd-upload-buffer", ALL)
{
    testUploadToBuffer(device);
}
