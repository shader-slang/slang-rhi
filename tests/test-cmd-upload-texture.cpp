#include "testing.h"

#include "texture-utils.h"
#include "texture-test.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("cmd-upload-texture-simple", D3D12 | Vulkan | WGPU)
{
    // Test all basic variants without initializing textures.
    TextureTestOptions options(device);
    options.addVariants(TTShape::All, TTArray::Both, TTMip::Both, TextureInitMode::None, TTFmtDepth::Off);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            // Get / re-init cpu side data with random data.
            TextureData& data = c->getTextureData();
            data.initData(TextureInitMode::Random);

            // fprintf(stderr, "Uploading texture %s\n", c->getTexture()->getDesc().label);

            // Create command encoder
            auto device = c->getDevice();
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Upload new texture data + wait
            commandEncoder->uploadTextureData(
                c->getTexture(),
                {0, data.desc.mipLevelCount, 0, data.desc.getLayerCount()},
                {0, 0, 0},
                Extents::kWholeTexture,
                data.subresourceData.data(),
                data.subresourceData.size()
            );
            queue->submit(commandEncoder->finish());
            queue->waitOnHost();

            // Verify it uploaded correctly
            data.checkEqual(c->getTexture());
        }
    );
}

GPU_TEST_CASE("cmd-upload-texture-single-layer", D3D12 | Vulkan | WGPU)
{
    // Test all basic variants without initializing textures.
    TextureTestOptions options(device);
    options.addVariants(TTShape::All, TTArray::On, TTMip::Both, TextureInitMode::Random, TTFmtDepth::Off);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            // Get / re-init cpu side data with random data.
            const TextureData& currentData = c->getTextureData();

            TextureData newData;
            newData.init(currentData.device, currentData.desc, TextureInitMode::Random, 1000);

            fprintf(stderr, "Uploading texture %s\n", c->getTexture()->getDesc().label);

            // Create command encoder
            auto device = c->getDevice();
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            uint32_t layerCount = currentData.desc.getLayerCount();

            // Replace alternate layers
            for (uint32_t layer = 0; layer < layerCount; layer++)
            {
                if ((layer % 2) == 1)
                {
                    // Copy the subresource data from inverted layer index to this layer
                    auto srdata = newData.getLayerFirstSubresourceData(layer);
                    commandEncoder->uploadTextureData(
                        c->getTexture(),
                        {0, newData.desc.mipLevelCount, layer, 1},
                        {0, 0, 0},
                        Extents::kWholeTexture,
                        srdata,
                        currentData.desc.mipLevelCount
                    );
                }
            }

            // Execute all operations
            queue->submit(commandEncoder->finish());
            queue->waitOnHost();

            // Verify alternate layers from original and new data
            for (uint32_t layer = 0; layer < layerCount; layer++)
            {
                if ((layer % 2) == 0)
                    currentData.checkLayersEqual(c->getTexture(), layer, layer);
                else
                    newData.checkLayersEqual(c->getTexture(), layer, layer);
            }
        }
    );
}

GPU_TEST_CASE("cmd-upload-texture-single-mip", D3D12 | Vulkan | WGPU)
{
    // Test all basic variants without initializing textures.
    TextureTestOptions options(device);
    options.addVariants(TTShape::All, TTArray::Off, TTMip::On, TextureInitMode::Random, TTFmtDepth::Off);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            // Get / re-init cpu side data with random data.
            const TextureData& currentData = c->getTextureData();

            TextureData newData;
            newData.init(currentData.device, currentData.desc, TextureInitMode::Random, 1000);

            // fprintf(stderr, "Uploading texture %s\n", c->getTexture()->getDesc().label);

            // Create command encoder
            auto device = c->getDevice();
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            uint32_t mipLevelCount = currentData.desc.mipLevelCount;

            // Replace alternate mipLevels
            for (uint32_t mipLevel = 0; mipLevel < mipLevelCount; mipLevel++)
            {
                if ((mipLevel % 2) == 1)
                {
                    // Copy the subresource data from inverted layer index to this layer
                    auto srdata = newData.getLayerFirstSubresourceData(0) + mipLevel;
                    commandEncoder->uploadTextureData(
                        c->getTexture(),
                        {mipLevel, 1, 0, 1},
                        {0, 0, 0},
                        Extents::kWholeTexture,
                        srdata,
                        1
                    );
                }
            }

            // Execute all operations
            queue->submit(commandEncoder->finish());
            queue->waitOnHost();

            // Verify alternate layers from original and new data
            for (uint32_t mipLevel = 0; mipLevel < mipLevelCount; mipLevel++)
            {
                if ((mipLevel % 2) == 0)
                    currentData.checkMipLevelsEqual(c->getTexture(), 0, mipLevel, 0, mipLevel);
                else
                    newData.checkMipLevelsEqual(c->getTexture(), 0, mipLevel, 0, mipLevel);
            }
        }
    );
}

/*
GPU_TEST_CASE("cmd-upload-texture-array", D3D12 | Vulkan | WGPU)
{
    testUploadTexture<ArrayUploadTexture>(device);
}
GPU_TEST_CASE("cmd-upload-texture-mips", D3D12 | Vulkan | WGPU)
{
    testUploadTexture<MipsUploadTexture>(device);
}
GPU_TEST_CASE("cmd-upload-texture-arraymips", D3D12 | Vulkan | WGPU)
{
    testUploadTexture<ArrayMipsUploadTexture>(device);
}
*/
