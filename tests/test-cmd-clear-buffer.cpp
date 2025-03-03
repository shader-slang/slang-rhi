#include "testing.h"

#include <string>
#include <map>
#include <functional>
#include <memory>

using namespace rhi;
using namespace rhi::testing;

void testClearBuffer(IDevice* device, size_t size, BufferRange range)
{
    std::unique_ptr<uint8_t[]> initialData(new uint8_t[size]);
    std::unique_ptr<uint8_t[]> expectedData(new uint8_t[size]);

    for (size_t i = 0; i < size; i++)
    {
        initialData[i] = (13 * i) & 0xff;
    }

    BufferDesc bufferDesc = {};
    bufferDesc.size = size;
    bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopyDestination | BufferUsage::CopySource;

    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initialData.get(), buffer.writeRef()));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        encoder->clearBuffer(buffer, range);
        queue->submit(encoder->finish());
        queue->waitOnHost();
    }

    std::memcpy(expectedData.get(), initialData.get(), size);
    for (size_t i = 0; i < size; i++)
    {
        if (i >= range.offset && i < range.offset + range.size)
        {
            expectedData[i] = 0;
        }
    }

    ComPtr<ISlangBlob> blob;
    REQUIRE_CALL(device->readBuffer(buffer, 0, size, blob.writeRef()));
    auto data = (uint8_t*)blob->getBufferPointer();
    for (int i = 0; i < size; i++)
    {
        CAPTURE(i);
        CHECK_EQ(data[i], expectedData[i]);
    }
}

GPU_TEST_CASE("cmd-clear-buffer", ALL)
{
    testClearBuffer(device, 128, kEntireBuffer);
    testClearBuffer(device, 128, {0, 4});
    testClearBuffer(device, 128, {0, 64});
    testClearBuffer(device, 128, {64, 4});
    testClearBuffer(device, 128, {64, 64});
    testClearBuffer(device, 128, {124, 4});
}
