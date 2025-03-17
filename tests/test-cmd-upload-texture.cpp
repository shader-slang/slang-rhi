#include "testing.h"

#include "texture-utils.h"
#include "texture-test.h"

#include "core/common.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("cmd-upload-texture-simple", D3D12 | Vulkan | Metal | CUDA | WGPU)
{
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

            // Verify it uploaded correctly
            data.checkEqual(c->getTexture());
        }
    );
}

GPU_TEST_CASE("cmd-upload-texture-single-layer", D3D12 | Vulkan | Metal | CUDA | WGPU)
{
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

GPU_TEST_CASE("cmd-upload-texture-single-mip", D3D12 | Vulkan | Metal | CUDA | WGPU)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::All, TTArray::On, TTMip::On, TextureInitMode::Random, TTFmtDepth::Off);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            // Get / re-init cpu side data with random data.
            const TextureData& currentData = c->getTextureData();

            TextureData newData;
            newData.init(currentData.device, currentData.desc, TextureInitMode::Random, 1000);

            // Create command encoder
            auto device = c->getDevice();
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            uint32_t mipLevelCount = currentData.desc.mipLevelCount;

            // Replace alternate mipLevels
            for (uint32_t layerIdx = 0; layerIdx < currentData.desc.getLayerCount(); layerIdx++)
            {
                for (uint32_t mipLevel = 0; mipLevel < mipLevelCount; mipLevel++)
                {
                    if ((mipLevel % 2) == 1)
                    {
                        // Copy the subresource data from inverted layer index to this layer
                        auto srdata = newData.getLayerFirstSubresourceData(layerIdx) + mipLevel;
                        commandEncoder->uploadTextureData(
                            c->getTexture(),
                            {mipLevel, 1, layerIdx, 1},
                            {0, 0, 0},
                            Extents::kWholeTexture,
                            srdata,
                            1
                        );
                    }
                }
            }

            // Execute all operations
            queue->submit(commandEncoder->finish());

            // Verify alternate layers from original and new data
            for (uint32_t layerIdx = 0; layerIdx < currentData.desc.getLayerCount(); layerIdx++)
            {
                for (uint32_t mipLevel = 0; mipLevel < mipLevelCount; mipLevel++)
                {
                    if ((mipLevel % 2) == 0)
                        currentData.checkMipLevelsEqual(c->getTexture(), layerIdx, mipLevel, layerIdx, mipLevel);
                    else
                        newData.checkMipLevelsEqual(c->getTexture(), layerIdx, mipLevel, layerIdx, mipLevel);
                }
            }
        }
    );
}

GPU_TEST_CASE("cmd-upload-texture-offset", D3D12 | Vulkan | Metal | CUDA | WGPU)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::All, TTArray::Both, TTMip::Off, TextureInitMode::Random, TTFmtDepth::Off);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            // Get / re-init cpu side data with random data.
            const TextureData& currentData = c->getTextureData();
            Extents size = currentData.desc.size;

            Offset3D offset = {size.width / 2, size.height / 2, size.depth / 2};

            TextureDesc newDesc = currentData.desc;
            newDesc.size.width = size.width - offset.x;
            newDesc.size.height = size.height - offset.y;
            newDesc.size.depth = size.depth - offset.z;

            TextureData newData;
            newData.init(currentData.device, newDesc, TextureInitMode::Random, 1000);

            // Create command encoder
            auto device = c->getDevice();
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();


            // Write an offset, still allowing remainder of texture to be written
            for (uint32_t layer = 0; layer < newDesc.getLayerCount(); layer++)
            {
                commandEncoder->uploadTextureData(
                    c->getTexture(),
                    {0, 1, layer, 1},
                    offset,
                    Extents::kWholeTexture,
                    newData.getLayerFirstSubresourceData(layer),
                    1
                );
            }

            // Execute all operations
            queue->submit(commandEncoder->finish());

            // Verify region. The inverse region should be the same as the original data,
            // and the interior of the region should match the new data.
            currentData.checkEqual(c->getTexture(), offset, Extents::kWholeTexture, true);
            newData.checkEqual(c->getTexture(), offset, Extents::kWholeTexture, false);
        }
    );
}

GPU_TEST_CASE("cmd-upload-texture-sizeoffset", D3D12 | Vulkan | Metal | CUDA | WGPU)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::All, TTArray::Both, TTMip::Off, TextureInitMode::Random, TTFmtDepth::Off);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            // Get / re-init cpu side data with random data.
            const TextureData& currentData = c->getTextureData();
            Extents size = currentData.desc.size;

            Offset3D offset = {size.width / 4, size.height / 4, size.depth / 4};
            Extents extents = {max(size.width / 4, 1), max(size.height / 4, 1), max(size.depth / 4, 1)};

            TextureDesc newDesc = currentData.desc;
            newDesc.size.width = extents.width;
            newDesc.size.height = extents.height;
            newDesc.size.depth = extents.depth;

            TextureData newData;
            newData.init(currentData.device, newDesc, TextureInitMode::Random, 1000);

            // Create command encoder
            auto device = c->getDevice();
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Write an offset, still allowing remainder of texture to be written
            for (uint32_t layer = 0; layer < newDesc.getLayerCount(); layer++)
            {
                commandEncoder->uploadTextureData(
                    c->getTexture(),
                    {0, 1, layer, 1},
                    offset,
                    extents,
                    newData.getLayerFirstSubresourceData(layer),
                    1
                );
            }

            // Execute all operations
            queue->submit(commandEncoder->finish());

            // Verify region. The inverse region should be the same as the original data,
            // and the interior of the region should match the new data.
            currentData.checkEqual(c->getTexture(), offset, extents, true);
            newData.checkEqual(c->getTexture(), offset, extents, false);
        }
    );
}

GPU_TEST_CASE("cmd-upload-texture-mipsizeoffset", D3D12 | Vulkan | Metal | CUDA | WGPU)
{
    TextureTestOptions options(device);
    options.addVariants(
        TTShape::D1 | TTShape::D2 | TTShape::D3, // no cube maps so we don't have to mess with layers
        TTArray::Off,
        TTMip::On,
        TextureInitMode::Random,
        TTFmtDepth::Off,
        TTFmtCompressed::Off // disable compressed formats as they're a pain with mip level calculations
    );

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            // Get / re-init cpu side data with random data.
            const TextureData& currentData = c->getTextureData();
            Extents size = currentData.desc.size;

            Offset3D offset = {size.width / 4, size.height / 4, size.depth / 4};
            Extents extents = {max(size.width / 4, 1), max(size.height / 4, 1), max(size.depth / 4, 1)};

            offset.x >>= 1;
            offset.y >>= 1;
            offset.z >>= 1;
            extents.width = max(extents.width >> 1, 1);
            extents.height = max(extents.height >> 1, 1);
            extents.depth = max(extents.depth >> 1, 1);

            TextureDesc newDesc = currentData.desc;
            newDesc.size.width = extents.width;
            newDesc.size.height = extents.height;
            newDesc.size.depth = extents.depth;

            TextureData newData;
            newData.init(currentData.device, newDesc, TextureInitMode::Random, 1000);

            // Create command encoder
            auto device = c->getDevice();
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Write an offset, still allowing remainder of texture to be written
            commandEncoder->uploadTextureData(
                c->getTexture(),
                {1, 1, 0, 1},
                offset,
                extents,
                newData.getLayerFirstSubresourceData(0),
                1
            );

            // Execute all operations
            queue->submit(commandEncoder->finish());

            // Verify top mip is untouched
            currentData.checkMipLevelsEqual(c->getTexture(), 0, 0, 0, 0);

            // Verify region. The inverse region should be the same as the original data,
            // and the interior of the region should match the new data.
            currentData.checkMipLevelsEqual(c->getTexture(), 0, 1, 0, 1, offset, extents, true);
            newData.checkMipLevelsEqual(c->getTexture(), 0, 0, 0, 1, offset, extents, false);
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
