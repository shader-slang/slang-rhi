#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

void testClearTexture(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    TextureDesc textureDesc = {};
    textureDesc.type = TextureType::Texture2D;
    textureDesc.mipLevelCount = 1;
    textureDesc.size.width = 4;
    textureDesc.size.height = 4;
    textureDesc.size.depth = 1;
    textureDesc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource | TextureUsage::CopyDestination;
    textureDesc.defaultState = ResourceState::RenderTarget;
    textureDesc.format = Format::R32G32B32A32_FLOAT;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(textureDesc, nullptr, texture.writeRef()));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        ClearValue clearValue = {};
        clearValue.color.floatValues[0] = 0.5f;
        clearValue.color.floatValues[1] = 1.0f;
        clearValue.color.floatValues[2] = 0.2f;
        clearValue.color.floatValues[3] = 0.1f;
        encoder->clearTexture(texture, clearValue);
        queue->submit(encoder->finish());
        queue->waitOnHost();

        ComPtr<ISlangBlob> blob;
        size_t rowPitch, pixelSize;
        device->readTexture(texture, blob.writeRef(), &rowPitch, &pixelSize);
        float* data = (float*)blob->getBufferPointer();
        for (int i = 0; i < 4; i++)
        {
            CHECK_EQ(data[i], clearValue.color.floatValues[i]);
        }
    }
}

TEST_CASE("clear-texture")
{
    // D3D11, Metal, CUDA, CPU don't support clearTexture
    runGpuTests(
        testClearTexture,
        {
            // DeviceType::D3D12, TODO: implement
            DeviceType::Vulkan,
        }
    );
}
