#include "testing.h"

#include "texture-utils.h"
#include "texture-test.h"
#include "resource-desc-utils.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("cmd-copy-texture-to-buffer-full", D3D12 | Vulkan | WGPU)
{
    TextureTestOptions options(device);
    options.addVariants(
        TTShape::All,    // all shapes
        TTArray::Both,   // array and non-array
        TTMip::Both,     // with/without mips
        TTMS::Both,      // with/without multisampling (when available)
        TTPowerOf2::Both // test both power-of-2 and non-power-of-2 sizes where possible
    );
    // options.addVariants(
    //    TTShape::D2,    // all shapes
    //    TTArray::Off,   // array and non-array
    //     Format::BC1Unorm,
    //    TTMip::On,     // with/without mips
    //    TTMS::Off,      // with/without multisampling (when available)
    //    TTPowerOf2::On // test both power-of-2 and non-power-of-2 sizes where possible
    //
    //);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();

            // Can't read-back ms or depth/stencil formats
            if (isMultisamplingType(data.desc.type))
                return;
            if (data.formatInfo.hasDepth && data.formatInfo.hasStencil)
                return;

            // Calculate total size needed for buffer
            // Note: need to ask texture its layout here, to get platform compatible strides
            uint64_t totalSize = 0;
            for (auto& subresource : data.subresources)
            {
                SubresourceLayout textureLayout;
                REQUIRE_CALL(c->getTexture()->getSubresourceLayout(subresource.mipLevel, &textureLayout));
                totalSize += textureLayout.sizeInBytes;
            }

            // Create a buffer large enough to hold the entire texture
            BufferDesc bufferDesc;
            bufferDesc.size = totalSize;
            bufferDesc.usage = BufferUsage::CopyDestination;
            bufferDesc.memoryType = MemoryType::ReadBack;
            ComPtr<IBuffer> buffer;
            REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Copy entire texture to buffer
            uint64_t bufferOffset = 0;
            for (uint32_t layer = 0; layer < data.desc.getLayerCount(); layer++)
            {
                for (uint32_t mip = 0; mip < data.desc.mipLevelCount; mip++)
                {
                    SubresourceLayout textureLayout;
                    REQUIRE_CALL(c->getTexture()->getSubresourceLayout(mip, &textureLayout));

                    commandEncoder->copyTextureToBuffer(
                        buffer,
                        bufferOffset,
                        textureLayout.sizeInBytes,
                        textureLayout.strideY,
                        c->getTexture(),
                        layer,
                        mip,
                        {0, 0, 0},
                        textureLayout.size
                    );

                    bufferOffset += textureLayout.sizeInBytes;
                }
            }
            queue->submit(commandEncoder->finish());

            queue->waitOnHost();

            // Verify buffer contents match texture data
            void* bufferData_;
            REQUIRE_CALL(device->mapBuffer(buffer, CpuAccessMode::Read, &bufferData_));
            uint8_t* bufferData = (uint8_t*)bufferData_;

            for (uint32_t layer = 0; layer < data.desc.getLayerCount(); layer++)
            {
                for (uint32_t mip = 0; mip < data.desc.mipLevelCount; mip++)
                {
                    SubresourceLayout textureLayout;
                    REQUIRE_CALL(c->getTexture()->getSubresourceLayout(mip, &textureLayout));

                    const TextureData::Subresource& subresource = data.getSubresource(layer, mip);

                    checkRegionsEqual(
                        bufferData,
                        textureLayout,
                        {0, 0, 0},
                        subresource.data.get(),
                        subresource.layout,
                        {0, 0, 0},
                        subresource.layout.size
                    );

                    bufferData += textureLayout.sizeInBytes;
                }
            }

            REQUIRE_CALL(device->unmapBuffer(buffer));
        }
    );
}

#if 0
GPU_TEST_CASE("cmd-copy-texture-to-buffer-partial", D3D12 | Vulkan | WGPU | CUDA)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::All, TTArray::Both, TTMip::Both, TTFmtDepth::Off);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();

            // Calculate size and offset for partial copy (1/4 of texture)
            SubresourceLayout layout;
            c->getTexture()->getSubresourceLayout(0, &layout);

            Extents copySize = layout.size;
            copySize.width = max(copySize.width / 2, 1u);
            copySize.height = max(copySize.height / 2, 1u);
            copySize.depth = max(copySize.depth / 2, 1u);

            Offset3D srcOffset = {
                max(layout.size.width / 4, 1u),
                max(layout.size.height / 4, 1u),
                max(layout.size.depth / 4, 1u)
            };

            // Align offsets and sizes to format block size
            srcOffset.x = math::calcAligned2(srcOffset.x, data.formatInfo.blockWidth);
            srcOffset.y = math::calcAligned2(srcOffset.y, data.formatInfo.blockHeight);
            copySize.width = math::calcAligned2(copySize.width, data.formatInfo.blockWidth);
            copySize.height = math::calcAligned2(copySize.height, data.formatInfo.blockHeight);

            // Create a buffer for the partial copy
            uint64_t bufferSize = copySize.width * copySize.height * copySize.depth * data.formatInfo.bytesPerBlock;
            BufferDesc bufferDesc;
            bufferDesc.sizeInBytes = bufferSize;
            bufferDesc.usage = BufferUsageFlags::CopyDst;
            ComPtr<IBuffer> buffer;
            REQUIRE_CALL(device->createBuffer(bufferDesc, &buffer));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Copy partial texture region to buffer
            uint64_t rowPitch = copySize.width * data.formatInfo.bytesPerBlock;
            uint64_t layerPitch = rowPitch * copySize.height;

            commandEncoder->copyTextureToBuffer(
                buffer,
                0,
                rowPitch,
                layerPitch,
                c->getTexture(),
                0, 0,
                srcOffset,
                copySize
            );
            queue->submit(commandEncoder->finish());

            // Verify buffer contents match texture data for copied region
            data.checkBufferEqual(
                buffer,
                0,
                rowPitch,
                layerPitch,
                0,
                0,
                srcOffset,
                copySize
            );
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-to-buffer-mips", D3D12 | Vulkan | WGPU | CUDA)
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

            // Get layout for mip level 1
            SubresourceLayout mip1Layout;
            c->getTexture()->getSubresourceLayout(1, &mip1Layout);

            // Create a buffer large enough for mip level 1
            uint64_t bufferSize = mip1Layout.size.width * mip1Layout.size.height * mip1Layout.size.depth * data.formatInfo.bytesPerBlock;
            BufferDesc bufferDesc;
            bufferDesc.sizeInBytes = bufferSize;
            bufferDesc.usage = BufferUsageFlags::CopyDst;
            ComPtr<IBuffer> buffer;
            REQUIRE_CALL(device->createBuffer(bufferDesc, &buffer));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Copy mip level 1 to buffer
            uint64_t rowPitch = mip1Layout.size.width * data.formatInfo.bytesPerBlock;
            uint64_t layerPitch = rowPitch * mip1Layout.size.height;

            commandEncoder->copyTextureToBuffer(
                buffer,
                0,
                rowPitch,
                layerPitch,
                c->getTexture(),
                0, 1,
                {0, 0, 0},
                mip1Layout.size
            );
            queue->submit(commandEncoder->finish());

            // Verify buffer contents match texture data for mip level 1
            data.checkBufferEqual(buffer, 0, rowPitch, layerPitch, 0, 1);
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-to-buffer-array", D3D12 | Vulkan | WGPU | CUDA)
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

            // Get layout for base mip level
            SubresourceLayout layout;
            c->getTexture()->getSubresourceLayout(0, &layout);

            // Create a buffer large enough for two array slices
            uint64_t sliceSize = layout.size.width * layout.size.height * layout.size.depth * data.formatInfo.bytesPerBlock;
            BufferDesc bufferDesc;
            bufferDesc.sizeInBytes = sliceSize * 2;
            bufferDesc.usage = BufferUsageFlags::CopyDst;
            ComPtr<IBuffer> buffer;
            REQUIRE_CALL(device->createBuffer(bufferDesc, &buffer));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Copy array slices 1 and 2 to buffer
            uint64_t rowPitch = layout.size.width * data.formatInfo.bytesPerBlock;
            uint64_t layerPitch = rowPitch * layout.size.height;

            for (uint32_t i = 0; i < 2; i++)
            {
                commandEncoder->copyTextureToBuffer(
                    buffer,
                    i * sliceSize,
                    rowPitch,
                    layerPitch,
                    c->getTexture(),
                    i + 1, 0,
                    {0, 0, 0},
                    layout.size
                );
            }
            queue->submit(commandEncoder->finish());

            // Verify buffer contents match texture data for both array slices
            for (uint32_t i = 0; i < 2; i++)
            {
                data.checkBufferEqual(buffer, i * sliceSize, rowPitch, layerPitch, i + 1, 0);
            }
        }
    );
}
#endif
