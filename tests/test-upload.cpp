#include "testing.h"

#include <string>
#include <map>
#include <functional>
#include <memory>

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

GPU_TEST_CASE("cmd-upload-buffer", ALL)
{
    testUploadToBuffer(device);
}
