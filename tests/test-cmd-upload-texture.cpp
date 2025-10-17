#include "testing.h"

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
                {0, data.desc.getLayerCount(), 0, data.desc.mipCount},
                {0, 0, 0},
                Extent3D::kWholeTexture,
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
                        {layer, 1, 0, newData.desc.mipCount},
                        {0, 0, 0},
                        Extent3D::kWholeTexture,
                        srdata,
                        currentData.desc.mipCount
                    );
                }
            }

            // Execute all operations
            queue->submit(commandEncoder->finish());

            // Verify alternate layers from original and new data
            for (uint32_t layer = 0; layer < layerCount; layer++)
            {
                if ((layer % 2) == 0)
                    currentData.checkLayersEqual(c->getTexture(), layer);
                else
                    newData.checkLayersEqual(c->getTexture(), layer);
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

            uint32_t mipCount = currentData.desc.mipCount;

            // Replace alternate mipLevels
            for (uint32_t layerIdx = 0; layerIdx < currentData.desc.getLayerCount(); layerIdx++)
            {
                for (uint32_t mip = 0; mip < mipCount; mip++)
                {
                    if ((mip % 2) == 1)
                    {
                        // Copy the subresource data from inverted layer index to this layer
                        auto srdata = newData.getLayerFirstSubresourceData(layerIdx) + mip;
                        commandEncoder->uploadTextureData(
                            c->getTexture(),
                            {layerIdx, 1, mip, 1},
                            {0, 0, 0},
                            Extent3D::kWholeTexture,
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
                for (uint32_t mip = 0; mip < mipCount; mip++)
                {
                    if ((mip % 2) == 0)
                        currentData.checkMipLevelsEqual(c->getTexture(), layerIdx, mip);
                    else
                        newData.checkMipLevelsEqual(c->getTexture(), layerIdx, mip);
                }
            }
        }
    );
}

GPU_TEST_CASE("cmd-upload-texture-multisubmit", D3D12 | Vulkan | Metal | CUDA | WGPU)
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

            uint32_t mipCount = currentData.desc.mipCount;

            auto queue = device->getQueue(QueueType::Graphics);

            // Replace mipLevels one at a time
            for (uint32_t layerIdx = 0; layerIdx < currentData.desc.getLayerCount(); layerIdx++)
            {
                for (uint32_t mip = 0; mip < mipCount; mip++)
                {
                    // Copy the subresource data from inverted layer index to this layer
                    auto commandEncoder = queue->createCommandEncoder();
                    auto srdata = newData.getLayerFirstSubresourceData(layerIdx) + mip;
                    commandEncoder->uploadTextureData(
                        c->getTexture(),
                        {layerIdx, 1, mip, 1},
                        {0, 0, 0},
                        Extent3D::kWholeTexture,
                        srdata,
                        1
                    );
                    queue->submit(commandEncoder->finish());
                }
            }

            queue->waitOnHost();

            // Verify everything now matches
            for (uint32_t layerIdx = 0; layerIdx < currentData.desc.getLayerCount(); layerIdx++)
            {
                for (uint32_t mip = 0; mip < mipCount; mip++)
                {
                    newData.checkMipLevelsEqual(c->getTexture(), layerIdx, mip);
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
            Extent3D size = currentData.desc.size;

            Offset3D offset = {size.width / 2, size.height / 2, size.depth / 2};
            offset.x = math::calcAligned2(offset.x, currentData.formatInfo.blockWidth);
            offset.y = math::calcAligned2(offset.y, currentData.formatInfo.blockHeight);

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
                    {layer, 1, 0, 1},
                    offset,
                    Extent3D::kWholeTexture,
                    newData.getLayerFirstSubresourceData(layer),
                    1
                );
            }

            // Execute all operations
            queue->submit(commandEncoder->finish());

            // Verify region. The inverse region should be the same as the original data,
            // and the interior of the region should match the new data.
            currentData.checkEqual(c->getTexture(), offset, Extent3D::kWholeTexture, true);
            newData.checkEqual({0, 0, 0}, c->getTexture(), offset, Extent3D::kWholeTexture, false);
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
            Extent3D size = currentData.desc.size;

            Offset3D offset = {size.width / 4, size.height / 4, size.depth / 4};
            offset.x = math::calcAligned2(offset.x, currentData.formatInfo.blockWidth);
            offset.y = math::calcAligned2(offset.y, currentData.formatInfo.blockHeight);

            Extent3D extent = {max(size.width / 4, 1u), max(size.height / 4, 1u), max(size.depth / 4, 1u)};
            extent.width = math::calcAligned2(extent.width, currentData.formatInfo.blockWidth);
            extent.height = math::calcAligned2(extent.height, currentData.formatInfo.blockHeight);

            TextureDesc newDesc = currentData.desc;
            newDesc.size.width = extent.width;
            newDesc.size.height = extent.height;
            newDesc.size.depth = extent.depth;

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
                    {layer, 1, 0, 1},
                    offset,
                    extent,
                    newData.getLayerFirstSubresourceData(layer),
                    1
                );
            }

            // Execute all operations
            queue->submit(commandEncoder->finish());

            // Verify region. The inverse region should be the same as the original data,
            // and the interior of the region should match the new data.
            currentData.checkEqual(c->getTexture(), offset, extent, true);
            newData.checkEqual({0, 0, 0}, c->getTexture(), offset, extent, false);
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
            Extent3D size = currentData.desc.size;

            Offset3D offset = {size.width / 4, size.height / 4, size.depth / 4};
            Extent3D extent = {max(size.width / 4, 1u), max(size.height / 4, 1u), max(size.depth / 4, 1u)};

            offset.x >>= 1;
            offset.y >>= 1;
            offset.z >>= 1;
            extent.width = max(extent.width >> 1, 1u);
            extent.height = max(extent.height >> 1, 1u);
            extent.depth = max(extent.depth >> 1, 1u);

            TextureDesc newDesc = currentData.desc;
            newDesc.size.width = extent.width;
            newDesc.size.height = extent.height;
            newDesc.size.depth = extent.depth;

            TextureData newData;
            newData.init(currentData.device, newDesc, TextureInitMode::Random, 1000);

            // Create command encoder
            auto device = c->getDevice();
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Write an offset, still allowing remainder of texture to be written
            commandEncoder->uploadTextureData(
                c->getTexture(),
                {0, 1, 1, 1},
                offset,
                extent,
                newData.getLayerFirstSubresourceData(0),
                1
            );

            // Execute all operations
            queue->submit(commandEncoder->finish());

            // Verify top mip is untouched
            currentData.checkMipLevelsEqual(0, 0, c->getTexture(), 0, 0);

            // Verify region. Mip 0 should be untouched and the chunk of mip 1 should match the new data
            currentData.checkMipLevelsEqual(c->getTexture(), 0, 0);
            newData.checkMipLevelsEqual(0, 0, {0, 0, 0}, c->getTexture(), 0, 1, offset, extent, false);
        }
    );
}
