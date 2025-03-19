#include "testing.h"

#include "texture-utils.h"
#include "texture-test.h"
#include "resource-desc-utils.h"


#include "core/common.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("cmd-copy-texture-full", D3D12 | Vulkan | WGPU | CUDA)
{
    TextureTestOptions options(device);
    options.addVariants(
        TTShape::All,    // all shapes
        TTArray::Both,   // array and non-array
        TTMip::Both,     // with/without mips
        TTMS::Both,      // with/without multisampling (when available)
        TTPowerOf2::Both // test both power-of-2 and non-power-of-2 sizes where possible
    );

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

GPU_TEST_CASE("cmd-copy-texture-arrayrange", D3D12 | Vulkan | WGPU | CUDA)
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

GPU_TEST_CASE("cmd-copy-texture-miprange", D3D12 | Vulkan | WGPU | CUDA)
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

GPU_TEST_CASE("cmd-copy-texture-fromarray", D3D12 | Vulkan | WGPU | CUDA)
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

GPU_TEST_CASE("cmd-copy-texture-toarray", D3D12 | Vulkan | WGPU | CUDA)
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

GPU_TEST_CASE("cmd-copy-texture-fromslice", D3D12 | Vulkan | WGPU | CUDA)
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

GPU_TEST_CASE("cmd-copy-texture-arrayfromslice", D3D12 | Vulkan | WGPU | CUDA)
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

GPU_TEST_CASE("cmd-copy-texture-toslice", D3D12 | Vulkan | WGPU | CUDA)
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
                {0, 0, 0},
                0,
                {0, 0, 1},
                {kRemainingTextureSize, kRemainingTextureSize, 1},
                false
            );
            data.checkLayersEqual(
                c->getTexture(),
                0,
                {0, 0, 1},
                0,
                {0, 0, 1},
                {kRemainingTextureSize, kRemainingTextureSize, 1},
                true
            );
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-offset-nomip", D3D12 | Vulkan | WGPU | CUDA)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::All, TTArray::Both, TTFmtDepth::Off);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();

            // Create a new texture with same descriptor
            TextureData newData;
            newData.init(device, data.desc, TextureInitMode::Random, 2132);
            ComPtr<ITexture> newTexture;
            REQUIRE_CALL(newData.createTexture(newTexture.writeRef()));

            Extents size = data.desc.size;
            Offset3D offset = {size.width / 4, size.height / 4, size.depth / 4};
            offset.x = math::calcAligned2(offset.x, data.formatInfo.blockWidth);
            offset.y = math::calcAligned2(offset.y, data.formatInfo.blockHeight);

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Copy at the offset, using kWholeTexture to express 'the rest of the texture'
            commandEncoder->copyTexture(
                newTexture,
                {0, 1, 0, 0},
                offset,
                c->getTexture(),
                {0, 1, 0, 0},
                offset,
                Extents::kWholeTexture
            );
            queue->submit(commandEncoder->finish());

            // Verify it uploaded correctly
            // The original texture data should have stomped over the new texture data
            // at offset.
            data.checkEqual(offset, newTexture, offset, Extents::kWholeTexture, false);
            newData.checkEqual(offset, newTexture, offset, Extents::kWholeTexture, true);
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-sizeoffset-nomip", D3D12 | Vulkan | WGPU | CUDA)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::All, TTArray::Both, TTFmtDepth::Off);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();

            // Create a new texture with same descriptor
            TextureData newData;
            newData.init(device, data.desc, TextureInitMode::Random, 2132);
            ComPtr<ITexture> newTexture;
            REQUIRE_CALL(newData.createTexture(newTexture.writeRef()));

            Extents size = data.desc.size;
            Offset3D offset = {size.width / 4, size.height / 4, size.depth / 4};
            offset.x = math::calcAligned2(offset.x, data.formatInfo.blockWidth);
            offset.y = math::calcAligned2(offset.y, data.formatInfo.blockHeight);

            Extents extents = {max(size.width / 4, 1), max(size.height / 4, 1), max(size.depth / 4, 1)};
            extents.width = math::calcAligned2(extents.width, data.formatInfo.blockWidth);
            extents.height = math::calcAligned2(extents.height, data.formatInfo.blockHeight);

            // fprintf(stderr, "Copy:\n  %s\n  %s\n", newTexture->getDesc().label, c->getTexture()->getDesc().label);

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Copy at the offset, using kWholeTexture to express 'the rest of the texture'
            commandEncoder
                ->copyTexture(newTexture, {0, 1, 0, 0}, offset, c->getTexture(), {0, 1, 0, 0}, offset, extents);

            queue->submit(commandEncoder->finish());

            // Verify it uploaded correctly
            // The original texture data should have stomped over the new texture data
            // at offset with given extents.
            data.checkEqual(offset, newTexture, offset, extents, false);
            newData.checkEqual(offset, newTexture, offset, extents, true);
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-smalltolarge", D3D12 | Vulkan | WGPU | CUDA)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::All, TTArray::Both, TTFmtDepth::Off);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& smallerData = c->getTextureData();
            ComPtr<ITexture> smallerTexture = c->getTexture();

            // Create a new larger texture with same descriptor
            TextureDesc largerDesc = smallerData.desc;
            largerDesc.size.width *= 2;
            if (largerDesc.size.height != 1)
                largerDesc.size.height *= 2;
            if (largerDesc.size.depth != 1)
                largerDesc.size.depth *= 2;
            TextureData largerData;
            largerData.init(device, largerDesc, TextureInitMode::Invalid);
            ComPtr<ITexture> largerTexture;
            REQUIRE_CALL(largerData.createTexture(largerTexture.writeRef()));

            Extents extents = smallerData.desc.size;
            Offset3D offset = {0, 0, 0};

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Copy at the offset, using kWholeTexture to express 'the rest of the texture'
            commandEncoder
                ->copyTexture(largerTexture, {0, 1, 0, 0}, offset, smallerTexture, {0, 1, 0, 0}, offset, extents);
            queue->submit(commandEncoder->finish());

            // Verify it uploaded correctly
            // The smaller texture should have overwritten the corner of
            // the larger texture.
            smallerData.checkEqual({0, 0, 0}, largerTexture, offset, extents, false);
            largerData.checkEqual(offset, largerTexture, offset, extents, true);
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-largetosmall", D3D12 | Vulkan | WGPU | CUDA)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::All, TTArray::Both, TTFmtDepth::Off);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& smallerData = c->getTextureData();
            ComPtr<ITexture> smallerTexture = c->getTexture();

            // Create a new larger texture with same descriptor
            TextureDesc largerDesc = smallerData.desc;
            largerDesc.size.width *= 4;
            if (largerDesc.size.height != 1)
                largerDesc.size.height *= 4;
            if (largerDesc.size.depth != 1)
                largerDesc.size.depth *= 4;
            TextureData largerData;
            largerData.init(device, largerDesc, TextureInitMode::Invalid);
            ComPtr<ITexture> largerTexture;
            REQUIRE_CALL(largerData.createTexture(largerTexture.writeRef()));

            // Going to copy an extent that is the size of the smaller texture,
            // with an offset based on its size (accounting for 1D/2D/3D dimensions)
            Extents extents = smallerData.desc.size;
            Offset3D offset;
            offset.x = smallerData.desc.size.width;
            if (smallerData.desc.size.height != 1)
                offset.y = smallerData.desc.size.height;
            if (smallerData.desc.size.depth != 1)
                offset.z = smallerData.desc.size.depth;

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Copy at the offset, using kWholeTexture to express 'the rest of the texture'
            commandEncoder
                ->copyTexture(smallerTexture, {0, 1, 0, 0}, {0, 0, 0}, largerTexture, {0, 1, 0, 0}, offset, extents);
            queue->submit(commandEncoder->finish());

            // Verify it uploaded correctly
            // The chunk of the larger texture we copied from should have overwritten the smaller texture
            largerData.checkEqual(offset, smallerTexture, {0, 0, 0}, extents, false);
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-acrossmips", D3D12 | Vulkan | WGPU | CUDA)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::All, TTArray::Both, TTMip::On, TTFmtDepth::Off);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& srcData = c->getTextureData();
            ComPtr<ITexture> srcTexture = c->getTexture();

            // Too painful to get mip calculations working for this test for non-power-of-2
            // block compressed textures!
            if (srcData.formatInfo.isCompressed && !math::isPowerOf2(srcData.desc.size.width))
                return;

            // Create a texture with same descriptor
            TextureDesc dstDesc = srcData.desc;
            TextureData dstData;
            dstData.init(device, dstDesc, TextureInitMode::Invalid);
            ComPtr<ITexture> dstTexture;
            REQUIRE_CALL(dstData.createTexture(dstTexture.writeRef()));

            // Going to copy an extent that is the size of mip1 from mip0
            SubresourceLayout srcMip1Layout;
            srcTexture->getSubresourceLayout(1, &srcMip1Layout);
            Extents extents = srcMip1Layout.size;

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Copy from mip 1 to mip 0
            commandEncoder
                ->copyTexture(dstTexture, {0, 1, 0, 0}, {0, 0, 0}, srcTexture, {1, 1, 0, 0}, {0, 0, 0}, extents);
            queue->submit(commandEncoder->finish());

            // Verify it uploaded correctly
            // The corner of mip 0 of the dst texture should have been overwritten by mip 1 of the src texture
            srcData.checkMipLevelsEqual(dstTexture, 0, 1, {0, 0, 0}, 0, 0, {0, 0, 0}, extents, false);
            dstData.checkMipLevelsEqual(dstTexture, 0, 0, {0, 0, 0}, 0, 0, {0, 0, 0}, extents, true);
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-offset-mip1", D3D12 | Vulkan | WGPU | CUDA)
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

            // Skip non-power-of-2 block compressed textures as they're too complex for mip calculations
            if (data.formatInfo.isCompressed && !math::isPowerOf2(data.desc.size.width))
                return;

            // Create a new texture with same descriptor
            TextureData newData;
            newData.init(device, data.desc, TextureInitMode::Random, 2132);
            ComPtr<ITexture> newTexture;
            REQUIRE_CALL(newData.createTexture(newTexture.writeRef()));

            // Get the size of mip level 1
            SubresourceLayout mip1Layout;
            c->getTexture()->getSubresourceLayout(1, &mip1Layout);
            Extents mip1Size = mip1Layout.size;

            // Calculate offset for mip level 1 (quarter of mip1 size)
            Offset3D offset = {mip1Size.width / 4, mip1Size.height / 4, mip1Size.depth / 4};
            offset.x = math::calcAligned2(offset.x, data.formatInfo.blockWidth);
            offset.y = math::calcAligned2(offset.y, data.formatInfo.blockHeight);

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Copy at the offset in mip level 1, using kWholeTexture to express 'the rest of the texture'
            commandEncoder->copyTexture(
                newTexture,
                {1, 1, 0, 0}, // Target mip level 1
                offset,
                c->getTexture(),
                {1, 1, 0, 0}, // Source from mip level 1
                offset,
                Extents::kWholeTexture
            );
            queue->submit(commandEncoder->finish());

            // Verify it uploaded correctly at mip level 1
            // The original texture data should have stomped over the new texture data
            // at offset in mip level 1.
            data.checkMipLevelsEqual(newTexture, 0, 1, offset, 0, 1, offset, Extents::kWholeTexture, false);
            newData.checkMipLevelsEqual(newTexture, 0, 1, offset, 0, 1, offset, Extents::kWholeTexture, true);
        }
    );
}
