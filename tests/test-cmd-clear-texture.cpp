#include "testing.h"

#include <string>
#include <map>
#include <functional>
#include <memory>

using namespace rhi;
using namespace rhi::testing;

#if 0
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
#endif

// D3D11, Metal, CUDA, CPU don't support clearTexture
GPU_TEST_CASE("cmd-clear-texture-float", Vulkan)
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

        float clearValue[4] = {0.5f, 1.0f, 0.2f, 0.1f};
        commandEncoder->clearTextureFloat(texture, kEntireTexture, clearValue);

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();

        ComPtr<ISlangBlob> blob;
        size_t rowPitch, pixelSize;
        device->readTexture(texture, blob.writeRef(), &rowPitch, &pixelSize);
        float* data = (float*)blob->getBufferPointer();
        for (int i = 0; i < 4; i++)
        {
            CHECK_EQ(data[i], clearValue[i]);
        }
    }
}

GPU_TEST_CASE("cmd-clear-texture-depth-stencil", D3D11 | D3D12 | Vulkan)
{
    // testCopyBuffer(device, 0, 0, 16);
    // testCopyBuffer(device, 0, 0, 8);
    // testCopyBuffer(device, 0, 8, 8);
    // testCopyBuffer(device, 8, 0, 8);
}
