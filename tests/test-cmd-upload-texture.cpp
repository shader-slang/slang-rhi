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

GPU_TEST_CASE("cmd-upload-texture-2d-nomip", D3D12 | Vulkan | WGPU)
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
