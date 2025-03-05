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

#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("cmd-upload-texture-2d-nomip", D3D12)
{
    TextureDesc textureDesc = {};
    textureDesc.type = TextureType::Texture2D;
    textureDesc.mipLevelCount = 1;
    textureDesc.size.width = 4;
    textureDesc.size.height = 4;
    textureDesc.size.depth = 1;
    // textureDesc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource | TextureUsage::CopyDestination;
    textureDesc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource | TextureUsage::CopyDestination;
    // textureDesc.defaultState = ResourceState::RenderTarget;
    textureDesc.defaultState = ResourceState::UnorderedAccess;
    textureDesc.format = Format::R32G32B32A32_FLOAT;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(textureDesc, nullptr, texture.writeRef()));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        // Generate random data.
        std::mt19937 rng(32);
        std::vector<float> srcPixels;
        std::uniform_real_distribution<float> dist(0, 1);
        srcPixels.resize(textureDesc.size.width * textureDesc.size.height * textureDesc.size.depth * 4);
        for (auto& val : srcPixels)
            val = dist(rng);

        size_t srcRowPitch = textureDesc.size.width * 4 * sizeof(float);
        float* srcData = srcPixels.data();

        SubresourceRange range = {0, 1, 0, 1};
        Offset3D offset = {0, 0, 0};
        Extents extent = {4, 4, 1};
        SubresourceData subresourceData = {srcData, srcRowPitch};
        commandEncoder->uploadTextureData(texture, range, offset, extent, &subresourceData, 1);

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();


        ComPtr<ISlangBlob> blob;
        size_t resultRowPitch, resultPixelSize;
        device->readTexture(texture, blob.writeRef(), &resultRowPitch, &resultPixelSize);
        float* resultData = (float*)blob->getBufferPointer();

        for (int y = 0; y < textureDesc.size.height; y++)
        {
            for (int x = 0; x < textureDesc.size.width; x++)
            {
                for (int c = 0; c < 4; c++)
                {
                    int index = x * 4 + c;
                    CHECK_EQ(resultData[index], srcData[index]);
                }
            }
            resultData += resultRowPitch / sizeof(float);
            srcData += srcRowPitch / sizeof(float);
        }
    }
}
