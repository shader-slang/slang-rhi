#if 0
#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

void testClearTexture(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    ComPtr<ITransientResourceHeap> transientHeap;
    ITransientResourceHeap::Desc transientHeapDesc = {};
    transientHeapDesc.constantBufferSize = 4096;
    REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

    TextureDesc srcTexDesc = {};
    srcTexDesc.type = TextureType::Texture2D;
    srcTexDesc.mipLevelCount = 1;
    srcTexDesc.size.width = 4;
    srcTexDesc.size.height = 4;
    srcTexDesc.size.depth = 1;
    srcTexDesc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource | TextureUsage::CopyDestination;
    srcTexDesc.defaultState = ResourceState::RenderTarget;
    srcTexDesc.format = Format::R32G32B32A32_FLOAT;

    ComPtr<ITexture> srcTexture;
    REQUIRE_CALL(device->createTexture(srcTexDesc, nullptr, srcTexture.writeRef()));

    {
        ICommandQueue::Desc queueDesc = {ICommandQueue::QueueType::Graphics};
        auto queue = device->createCommandQueue(queueDesc);

        auto commandBuffer = transientHeap->createCommandBuffer();
        auto resourceEncoder = commandBuffer->encodeResourceCommands();
        ClearValue clearValue = {};
        clearValue.color.floatValues[0] = 0.5f;
        clearValue.color.floatValues[1] = 1.0f;
        clearValue.color.floatValues[2] = 0.2f;
        clearValue.color.floatValues[3] = 0.1f;
        resourceEncoder->clearResourceView(rtv, &clearValue, ClearResourceViewFlags::FloatClearValues);
        resourceEncoder->endEncoding();

        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);

        queue->waitOnHost();

        ComPtr<ISlangBlob> blob;
        size_t rowPitch, pixelSize;
        device->readTexture(srcTexture, blob.writeRef(), &rowPitch, &pixelSize);
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
            DeviceType::D3D12,
            DeviceType::Vulkan,
        }
    );
}
#endif
