#include "testing.h"

#include <string>
#include <map>
#include <functional>
#include <memory>

using namespace rhi;
using namespace rhi::testing;

void testCopyBuffer(IDevice* device, Offset dstOffset, Offset srcOffset, Size size)
{
    uint8_t srcData[] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF};
    uint8_t dstData[] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

    uint8_t expected[16];
    std::memcpy(expected, dstData, 16);
    std::memcpy(expected + dstOffset, srcData + srcOffset, size);

    BufferDesc bufferDesc = {};
    bufferDesc.size = 16;
    bufferDesc.usage = BufferUsage::CopyDestination | BufferUsage::CopySource;

    ComPtr<IBuffer> src;
    ComPtr<IBuffer> dst;
    REQUIRE_CALL(device->createBuffer(bufferDesc, srcData, src.writeRef()));
    REQUIRE_CALL(device->createBuffer(bufferDesc, dstData, dst.writeRef()));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        encoder->copyBuffer(dst, dstOffset, src, srcOffset, size);
        queue->submit(encoder->finish());
        queue->waitOnHost();

        ComPtr<ISlangBlob> blob;
        REQUIRE_CALL(device->readBuffer(dst, 0, 16, blob.writeRef()));
        auto data = (uint8_t*)blob->getBufferPointer();
        for (int i = 0; i < 16; i++)
        {
            CHECK_EQ(data[i], expected[i]);
        }
    }
}

GPU_TEST_CASE("cmd-copy-buffer", ALL)
{
    testCopyBuffer(device, 0, 0, 16);
    testCopyBuffer(device, 0, 0, 8);
    testCopyBuffer(device, 0, 8, 8);
    testCopyBuffer(device, 8, 0, 8);
}
