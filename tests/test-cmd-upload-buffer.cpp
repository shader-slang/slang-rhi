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

Device* getSharedDevice(IDevice* device)
{
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
    // Ensure any previous operations have finished so we can safely check heap usage.
    device->getQueue(QueueType::Graphics)->waitOnHost();

    StagingHeap& heap = getSharedDevice(device)->m_uploadHeap;
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
