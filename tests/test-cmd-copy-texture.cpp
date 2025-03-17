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

            fprintf(stderr, "Uploading texture %s\n", c->getTexture()->getDesc().label);

            // Create a new, uninitialized texture with same descriptor
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

            fprintf(stderr, "Uploading texture %s\n", c->getTexture()->getDesc().label);

            // Create a new, uninitialized texture with same descriptor
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
