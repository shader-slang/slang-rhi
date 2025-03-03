#include "testing.h"

#include <string>
#include <map>
#include <functional>
#include <memory>

#include "rhi-shared.h"

using namespace rhi;
using namespace rhi::testing;

void testUploadToBuffer(IDevice* device)
{
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
        queue->submit(encoder->finish());
        queue->waitOnHost();

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

    auto allocation = heap.alloc(16, {2});
    heap.checkConsistency();

    Size alloc_size = heap.alignUp(16);

    CHECK_EQ(allocation.getOffset(), 0);
    CHECK_EQ(allocation.getSize(), alloc_size);
    CHECK_EQ(allocation.getMetaData().use, 2);
    CHECK_EQ(allocation.getPageId(), 1);
    CHECK_EQ(heap.getUsed(), alloc_size);

    auto allocation2 = heap.alloc(16, {3});
    heap.checkConsistency();

    CHECK_EQ(allocation2.getOffset(), alloc_size);
    CHECK_EQ(allocation2.getSize(), alloc_size);
    CHECK_EQ(allocation2.getMetaData().use, 3);
    CHECK_EQ(allocation2.getPageId(), 1);
    CHECK_EQ(heap.getUsed(), alloc_size * 2);

    heap.free(allocation);
    heap.checkConsistency();

    CHECK_EQ(heap.getUsed(), alloc_size);

    heap.free(allocation2);
    heap.checkConsistency();

    CHECK_EQ(heap.getUsed(), 0);
}

GPU_TEST_CASE("cmd-upload-buffer", ALL)
{
    testUploadToBuffer(device);
}
