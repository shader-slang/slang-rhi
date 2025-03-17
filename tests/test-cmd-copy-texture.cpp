#include "testing.h"

#include "texture-utils.h"
#include "texture-test.h"
#include "resource-desc-utils.h"


#include "core/common.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("cmd-copy-texture-full", D3D12 | Vulkan | WGPU)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::All, TTArray::Both, TTMip::Both, TTMS::Both);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();

            // Create a new, uninitialized texture with same descriptor
            TextureData newData;
            newData.init(device, data.desc, TextureInitMode::None);
            ComPtr<ITexture> newTexture;
            REQUIRE_CALL(newData.createTexture(newTexture.writeRef()));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Copy all subresources with offsets at 0 and size of whole texture.
            commandEncoder->copyTexture(
                newTexture,
                {0, 0, 0, 0},
                {0, 0, 0},
                c->getTexture(),
                {0, 0, 0, 0},
                {0, 0, 0},
                Extents::kWholeTexture
            );
            queue->submit(commandEncoder->finish());

            // Can't read-back ms or depth/stencil formats
            if (isMultisamplingType(data.desc.type))
                return;
            if (data.formatInfo.hasDepth && data.formatInfo.hasStencil)
                return;

            // Verify it uploaded correctly
            data.checkEqual(newTexture);
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-arrayrange", D3D12 | Vulkan | WGPU)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::All, TTArray::On, TTMip::Both, TTFmtDepth::Off);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();

            // fprintf(stderr, "Uploading texture %s\n", c->getTexture()->getDesc().label);

            // Create a new, random texture with same descriptor
            TextureData newData;
            newData.init(device, data.desc, TextureInitMode::Random, 1323);
            ComPtr<ITexture> newTexture;
            REQUIRE_CALL(newData.createTexture(newTexture.writeRef()));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            uint32_t halfLayerCount = data.desc.arrayLength / 2;

            // Copy the 1st half of the layers to the 2nd half
            commandEncoder->copyTexture(
                newTexture,
                {0, 0, 0, halfLayerCount},
                {0, 0, 0},
                c->getTexture(),
                {0, 0, halfLayerCount, halfLayerCount},
                {0, 0, 0},
                Extents::kWholeTexture
            );
            queue->submit(commandEncoder->finish());

            // Verify 1st half of layers are stomped over by 2nd half
            // of the source texture.
            for (uint32_t i = 0; i < halfLayerCount; i++)
            {
                // 1st half should be equal to 2nd half of source
                data.checkLayersEqual(newTexture, i + halfLayerCount, i);

                // 2nd half should be unchanged
                newData.checkLayersEqual(newTexture, i + halfLayerCount, i + halfLayerCount);
            }
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-miprange", D3D12 | Vulkan | WGPU)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::All, TTArray::Both, TTMip::On, TTFmtDepth::Off);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();

            // Create a new, random texture with same descriptor
            TextureData newData;
            newData.init(device, data.desc, TextureInitMode::Random, 1323);
            ComPtr<ITexture> newTexture;
            REQUIRE_CALL(newData.createTexture(newTexture.writeRef()));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            uint32_t halfMipCount = calcMipLevelCount(data.desc) / 2;

            // Copy the 1st half of the mips for all layers
            commandEncoder->copyTexture(
                newTexture,
                {0, halfMipCount, 0, 0},
                {0, 0, 0},
                c->getTexture(),
                {0, halfMipCount, 0, 0},
                {0, 0, 0},
                Extents::kWholeTexture
            );
            queue->submit(commandEncoder->finish());

            // Verify 1st half of layers are stomped over by 2nd half
            // of the source texture.
            for (uint32_t layer = 0; layer < data.desc.getLayerCount(); layer++)
            {
                for (uint32_t mipLevel = 0; mipLevel < halfMipCount; mipLevel++)
                {
                    // 1st half should be the copy
                    data.checkMipLevelsEqual(newTexture, layer, mipLevel, layer, mipLevel);

                    // 2nd half should be unchanged
                    newData.checkMipLevelsEqual(
                        newTexture,
                        layer,
                        mipLevel + halfMipCount,
                        layer,
                        mipLevel + halfMipCount
                    );
                }
            }
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-fromarray", D3D12 | Vulkan | WGPU)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::D1 | TTShape::D2, TTArray::On, TTMip::Both, TTFmtDepth::Off);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();

            // Create a new, uninitialized texture that has the same properties
            // but is not an array.
            TextureDesc newDesc = data.desc;
            newDesc.arrayLength = 1;
            REQUIRE(getScalarType(data.desc.type, newDesc.type));
            TextureData newData;
            newData.init(device, newDesc, TextureInitMode::None);
            ComPtr<ITexture> newTexture;
            REQUIRE_CALL(newData.createTexture(newTexture.writeRef()));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // fprintf(stderr, "Copy:\n\t%s\n\t%s\n", newTexture->getDesc().label, c->getTexture()->getDesc().label);

            // Copy from layer 2 into layer 0
            commandEncoder->copyTexture(
                newTexture,
                {0, 0, 0, 1},
                {0, 0, 0},
                c->getTexture(),
                {0, 0, 2, 1},
                {0, 0, 0},
                Extents::kWholeTexture
            );
            queue->submit(commandEncoder->finish());

            // Check layers now match.
            data.checkLayersEqual(newTexture, 2, 0);
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-toarray", D3D12 | Vulkan | WGPU)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::D1 | TTShape::D2, TTArray::On, TTMip::Both, TTFmtDepth::Off, TextureInitMode::None);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();

            // fprintf(stderr, "Uploading texture %s\n", c->getTexture()->getDesc().label);

            // Create a new, random texture that has the same properties
            // but is not an array.
            TextureDesc newDesc = data.desc;
            newDesc.arrayLength = 1;
            REQUIRE(getScalarType(data.desc.type, newDesc.type));
            TextureData newData;
            newData.init(device, newDesc, TextureInitMode::Random);
            ComPtr<ITexture> newTexture;
            REQUIRE_CALL(newData.createTexture(newTexture.writeRef()));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // fprintf(stderr, "Copy:\n\t%s\n\t%s\n", newTexture->getDesc().label, c->getTexture()->getDesc().label);

            // Copy from layer 0 of the new texture into layer 2
            // of the one allocated by the testing system.
            commandEncoder->copyTexture(
                c->getTexture(),
                {0, 0, 2, 1},
                {0, 0, 0},
                newTexture,
                {0, 0, 0, 1},
                {0, 0, 0},
                Extents::kWholeTexture
            );
            queue->submit(commandEncoder->finish());

            // Check layers now match.
            newData.checkLayersEqual(c->getTexture(), 0, 2);
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-fromslice", D3D12 | Vulkan | WGPU)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::D3, TTArray::Off, TTMip::Both, TTFmtDepth::Off);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();

            // Currently slice copies of 12B formats is disabled due to poor D3D12 support.
            if (data.formatInfo.blockSizeInBytes == 12)
                return;

            // Create a new, uninitialized 2d texture with the
            // same width/height.
            TextureDesc newDesc = data.desc;
            newDesc.type = TextureType::Texture2D;
            newDesc.size.depth = 1;
            TextureData newData;
            newData.init(device, newDesc, TextureInitMode::Invalid);
            ComPtr<ITexture> newTexture;
            REQUIRE_CALL(newData.createTexture(newTexture.writeRef()));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // fprintf(stderr, "Copy:\n\t%s\n\t%s\n", newTexture->getDesc().label, c->getTexture()->getDesc().label);

            // Copy 1 slice with a depth offset
            commandEncoder->copyTexture(
                newTexture,
                {0, 1, 0, 1},
                {0, 0, 0},
                c->getTexture(),
                {0, 1, 0, 1},
                {0, 0, 1},
                {kRemainingTextureSize, kRemainingTextureSize, 1}
            );
            queue->submit(commandEncoder->finish());

            // Check the slice now matches the texture.
            data.checkSliceEqual(newTexture, 0, 0, 1, 0, 0);
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-arrayfromslice", D3D12 | Vulkan | WGPU)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::D3, TTArray::Off, TTMip::Both, TTFmtDepth::Off);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();

            // Currently slice copies of 12B formats is disabled due to poor D3D12 support.
            if (data.formatInfo.blockSizeInBytes == 12)
                return;

            // Create a new, uninitialized 2d array texture with the
            // same width/height.
            TextureDesc newDesc = data.desc;
            newDesc.type = TextureType::Texture2DArray;
            newDesc.size.depth = 1;
            newDesc.arrayLength = 4;
            TextureData newData;
            newData.init(device, newDesc, TextureInitMode::Invalid);
            ComPtr<ITexture> newTexture;
            REQUIRE_CALL(newData.createTexture(newTexture.writeRef()));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // fprintf(stderr, "Copy:\n\t%s\n\t%s\n", newTexture->getDesc().label, c->getTexture()->getDesc().label);

            // Copy 1 slice with a depth offset
            commandEncoder->copyTexture(
                newTexture,
                {0, 1, 2, 1},
                {0, 0, 0},
                c->getTexture(),
                {0, 1, 0, 1},
                {0, 0, 1},
                {kRemainingTextureSize, kRemainingTextureSize, 1}
            );
            queue->submit(commandEncoder->finish());

            // Check the slice now matches the texture.
            data.checkSliceEqual(newTexture, 0, 0, 1, 2, 0);
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-toslice", D3D12 | Vulkan | WGPU)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::D3, TTArray::Off, TTMip::Off, TTFmtDepth::Off, TextureInitMode::Invalid);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();

            // Currently slice copies of 12B formats is disabled due to poor D3D12 support.
            if (data.formatInfo.blockSizeInBytes == 12)
                return;

            // Create a new, random 2d texture with the
            // same width/height.
            TextureDesc newDesc = data.desc;
            newDesc.type = TextureType::Texture2D;
            newDesc.size.depth = 1;
            TextureData newData;
            newData.init(device, newDesc, TextureInitMode::Random, 2131);
            ComPtr<ITexture> newTexture;
            REQUIRE_CALL(newData.createTexture(newTexture.writeRef()));


            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // fprintf(stderr, "Copy:\n\t%s\n\t%s\n", newTexture->getDesc().label, c->getTexture()->getDesc().label);

            // Copy from the new texture into slice 1 of the texture allocated by the testing system
            commandEncoder->copyTexture(
                c->getTexture(),
                {0, 1, 0, 1},
                {0, 0, 1},
                newTexture,
                {0, 1, 0, 1},
                {0, 0, 0},
                {kRemainingTextureSize, kRemainingTextureSize, 1}
            );
            queue->submit(commandEncoder->finish());

            // Check layers now match inside and outside of the region.
            newData.checkLayersEqual(
                c->getTexture(),
                0,
                0,
                {0, 0, 1},
                {kRemainingTextureSize, kRemainingTextureSize, 1},
                false
            );
            data.checkLayersEqual(
                c->getTexture(),
                0,
                0,
                {0, 0, 1},
                {kRemainingTextureSize, kRemainingTextureSize, 1},
                true
            );
        }
    );
}
