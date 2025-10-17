#include "testing.h"

#include "texture-test.h"
#include "resource-desc-utils.h"

using namespace rhi;
using namespace rhi::testing;


Result getSizeAndMakeBuffer(TextureTestContext* c, uint64_t* outSize, IBuffer** outBuffer)
{
    // Calculate total size needed for buffer
    // Note: need to ask texture its layout here, to get platform compatible strides
    uint64_t totalSize = 0;
    for (auto& subresource : c->getTextureData().subresources)
    {
        SubresourceLayout textureLayout;
        SLANG_RETURN_ON_FAIL(c->getTexture()->getSubresourceLayout(subresource.mip, &textureLayout));
        totalSize += textureLayout.sizeInBytes;
    }
    *outSize = totalSize;

    // Create a buffer large enough to hold the entire texture
    BufferDesc bufferDesc;
    bufferDesc.size = totalSize;
    bufferDesc.usage = BufferUsage::CopyDestination;
    bufferDesc.memoryType = MemoryType::ReadBack;
    return c->getDevice()->createBuffer(bufferDesc, nullptr, outBuffer);
}

GPU_TEST_CASE("cmd-copy-texture-to-buffer-full", D3D12 | Vulkan | Metal | WGPU | CUDA)
{
    TextureTestOptions options(device);
    options.addVariants(
        TTShape::All,     // all shapes
        TTArray::Both,    // array and non-array
        TTMip::Both,      // with/without mips
        TTMS::Off,        // without multisampling
        TTPowerOf2::Both, // test both power-of-2 and non-power-of-2 sizes where possible
        TTFmtStencil::Off // no stencil formats
    );

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();
            uint64_t totalSize;
            ComPtr<IBuffer> buffer;
            REQUIRE_CALL(getSizeAndMakeBuffer(c, &totalSize, buffer.writeRef()));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Copy entire texture to buffer
            uint64_t bufferOffset = 0;
            for (uint32_t layer = 0; layer < data.desc.getLayerCount(); layer++)
            {
                for (uint32_t mip = 0; mip < data.desc.mipCount; mip++)
                {
                    SubresourceLayout textureLayout;
                    REQUIRE_CALL(c->getTexture()->getSubresourceLayout(mip, &textureLayout));

                    commandEncoder->copyTextureToBuffer(
                        buffer,
                        bufferOffset,
                        textureLayout.sizeInBytes,
                        textureLayout.rowPitch,
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
            void* bufferData_ = nullptr;
            REQUIRE_CALL(device->mapBuffer(buffer, CpuAccessMode::Read, &bufferData_));
            uint8_t* bufferData = (uint8_t*)bufferData_;

            for (uint32_t layer = 0; layer < data.desc.getLayerCount(); layer++)
            {
                for (uint32_t mip = 0; mip < data.desc.mipCount; mip++)
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

// Tests copying data at a different alignment to that returned by getSubresourceLayout
GPU_TEST_CASE("cmd-copy-texture-to-buffer-rowalignment", D3D12 | Vulkan | Metal | WGPU | CUDA)
{
    TextureTestOptions options(device);
    options.addVariants(
        TTShape::All,     // all shapes
        TTArray::Both,    // array and non-array
        TTMip::Both,      // with/without mips
        TTMS::Off,        // without multisampling
        TTPowerOf2::Both, // test both power-of-2 and non-power-of-2 sizes where possible
        TTFmtStencil::Off // no stencil formats
    );

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();

            uint32_t customAlignment = 512;

            // Calculate total size needed for buffer
            // Note: need to ask texture its layout here, to get platform compatible strides
            uint64_t totalSize = 0;
            for (auto& subresource : data.subresources)
            {
                SubresourceLayout textureLayout;
                REQUIRE_CALL(c->getTexture()->getSubresourceLayout(subresource.mip, &textureLayout));

                textureLayout.rowPitch = math::calcAligned2(textureLayout.rowPitch, customAlignment);
                textureLayout.slicePitch = textureLayout.rowPitch * textureLayout.rowCount;
                textureLayout.sizeInBytes = textureLayout.slicePitch * textureLayout.size.depth;

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
                for (uint32_t mip = 0; mip < data.desc.mipCount; mip++)
                {
                    SubresourceLayout textureLayout;
                    REQUIRE_CALL(c->getTexture()->getSubresourceLayout(mip, &textureLayout));

                    textureLayout.rowPitch = math::calcAligned2(textureLayout.rowPitch, customAlignment);
                    textureLayout.slicePitch = textureLayout.rowPitch * textureLayout.rowCount;
                    textureLayout.sizeInBytes = textureLayout.slicePitch * textureLayout.size.depth;

                    commandEncoder->copyTextureToBuffer(
                        buffer,
                        bufferOffset,
                        textureLayout.sizeInBytes,
                        textureLayout.rowPitch,
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
            void* bufferData_ = nullptr;
            REQUIRE_CALL(device->mapBuffer(buffer, CpuAccessMode::Read, &bufferData_));
            uint8_t* bufferData = (uint8_t*)bufferData_;

            for (uint32_t layer = 0; layer < data.desc.getLayerCount(); layer++)
            {
                for (uint32_t mip = 0; mip < data.desc.mipCount; mip++)
                {
                    SubresourceLayout textureLayout;
                    REQUIRE_CALL(c->getTexture()->getSubresourceLayout(mip, &textureLayout));

                    textureLayout.rowPitch = math::calcAligned2(textureLayout.rowPitch, customAlignment);
                    textureLayout.slicePitch = textureLayout.rowPitch * textureLayout.rowCount;
                    textureLayout.sizeInBytes = textureLayout.slicePitch * textureLayout.size.depth;

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

GPU_TEST_CASE("cmd-copy-texture-to-buffer-offset", D3D12 | Vulkan | Metal | WGPU | CUDA)
{
    TextureTestOptions options(device);
    options.addVariants(
        TTShape::All,     // all shapes
        TTArray::Both,    // array and non-array
        TTMip::Off,       // without mips
        TTMS::Off,        // without multisampling
        TTPowerOf2::Both, // test both power-of-2 and non-power-of-2 sizes where possible
        TTFmtStencil::Off // no stencil formats
    );

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();
            uint64_t totalSize = 0;
            ComPtr<IBuffer> buffer;
            REQUIRE_CALL(getSizeAndMakeBuffer(c, &totalSize, buffer.writeRef()));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Horrible but only way to zero out the read-back buffer is to copy to it from another
            // cleared buffer!
            ComPtr<IBuffer> zeroBuffer;
            BufferDesc zeroBufferDesc;
            zeroBufferDesc.size = totalSize;
            zeroBufferDesc.usage =
                BufferUsage::CopySource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination;
            device->createBuffer(zeroBufferDesc, nullptr, zeroBuffer.writeRef());
            commandEncoder->clearBuffer(zeroBuffer);
            commandEncoder->copyBuffer(buffer, 0, zeroBuffer, 0, totalSize);

            // Pick offset for the extra copy.
            Extent3D size = data.desc.size;
            Offset3D offset = {size.width / 4, size.height / 4, size.depth / 4};
            offset.x = math::calcAligned2(offset.x, data.formatInfo.blockWidth);
            offset.y = math::calcAligned2(offset.y, data.formatInfo.blockHeight);

            // Copy region of 2nd texture
            uint64_t bufferOffset = 0;
            for (uint32_t layer = 0; layer < data.desc.getLayerCount(); layer++)
            {
                SubresourceLayout textureLayout;
                REQUIRE_CALL(c->getTexture()->getSubresourceLayout(0, &textureLayout));

                commandEncoder->copyTextureToBuffer(
                    buffer,
                    bufferOffset,
                    textureLayout.sizeInBytes,
                    textureLayout.rowPitch,
                    c->getTexture(),
                    layer,
                    0,
                    offset,
                    Extent3D::kWholeTexture
                );

                bufferOffset += textureLayout.sizeInBytes;
            }
            queue->submit(commandEncoder->finish());

            queue->waitOnHost();

            // Verify buffer contents match texture data
            void* bufferData_ = nullptr;
            REQUIRE_CALL(device->mapBuffer(buffer, CpuAccessMode::Read, &bufferData_));
            uint8_t* bufferData = (uint8_t*)bufferData_;

            Extent3D checkExtents = {size.width - offset.x, size.height - offset.y, size.depth - offset.z};

            for (uint32_t layer = 0; layer < data.desc.getLayerCount(); layer++)
            {
                SubresourceLayout textureLayout;
                REQUIRE_CALL(c->getTexture()->getSubresourceLayout(0, &textureLayout));

                const TextureData::Subresource& subresource = data.getSubresource(layer, 0);

                // Adjust stride between layers to account for smaller region in 3d texture.
                textureLayout.slicePitch = textureLayout.rowPitch * checkExtents.height / data.formatInfo.blockHeight;

                checkRegionsEqual(
                    bufferData,
                    textureLayout,
                    {0, 0, 0},
                    subresource.data.get(),
                    subresource.layout,
                    offset,
                    checkExtents
                );

                bufferData += textureLayout.sizeInBytes;
            }

            REQUIRE_CALL(device->unmapBuffer(buffer));
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-to-buffer-sizeoffset", D3D12 | Vulkan | Metal | WGPU | CUDA)
{
    TextureTestOptions options(device);
    options.addVariants(
        TTShape::All,     // all shapes
        TTArray::Both,    // array and non-array
        TTMip::Off,       // without mips
        TTMS::Off,        // without multisampling
        TTPowerOf2::Both, // test both power-of-2 and non-power-of-2 sizes where possible
        TTFmtStencil::Off // no stencil formats
    );

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();
            uint64_t totalSize = 0;
            ComPtr<IBuffer> buffer;
            REQUIRE_CALL(getSizeAndMakeBuffer(c, &totalSize, buffer.writeRef()));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Horrible but only way to zero out the read-back buffer is to copy to it from another
            // cleared buffer!
            ComPtr<IBuffer> zeroBuffer;
            BufferDesc zeroBufferDesc;
            zeroBufferDesc.size = totalSize;
            zeroBufferDesc.usage =
                BufferUsage::CopySource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination;
            device->createBuffer(zeroBufferDesc, nullptr, zeroBuffer.writeRef());
            commandEncoder->clearBuffer(zeroBuffer);
            commandEncoder->copyBuffer(buffer, 0, zeroBuffer, 0, totalSize);

            // Pick offset for the extra copy.
            Extent3D size = data.desc.size;
            Offset3D offset = {size.width / 4, size.height / 4, size.depth / 4};
            offset.x = math::calcAligned2(offset.x, data.formatInfo.blockWidth);
            offset.y = math::calcAligned2(offset.y, data.formatInfo.blockHeight);

            Extent3D copySize = {max(size.width / 2, 1u), max(size.height / 2, 1u), max(size.depth / 2, 1u)};
            copySize.width = math::calcAligned2(copySize.width, data.formatInfo.blockWidth);
            copySize.height = math::calcAligned2(copySize.height, data.formatInfo.blockHeight);

            // Copy region of 2nd texture
            uint64_t bufferOffset = 0;
            for (uint32_t layer = 0; layer < data.desc.getLayerCount(); layer++)
            {
                SubresourceLayout textureLayout;
                REQUIRE_CALL(c->getTexture()->getSubresourceLayout(0, &textureLayout));

                commandEncoder->copyTextureToBuffer(
                    buffer,
                    bufferOffset,
                    textureLayout.sizeInBytes,
                    textureLayout.rowPitch,
                    c->getTexture(),
                    layer,
                    0,
                    offset,
                    copySize
                );

                bufferOffset += textureLayout.sizeInBytes;
            }
            queue->submit(commandEncoder->finish());

            queue->waitOnHost();

            // Verify buffer contents match texture data
            void* bufferData_ = nullptr;
            REQUIRE_CALL(device->mapBuffer(buffer, CpuAccessMode::Read, &bufferData_));
            uint8_t* bufferData = (uint8_t*)bufferData_;

            for (uint32_t layer = 0; layer < data.desc.getLayerCount(); layer++)
            {
                SubresourceLayout textureLayout;
                REQUIRE_CALL(c->getTexture()->getSubresourceLayout(0, &textureLayout));

                const TextureData::Subresource& subresource = data.getSubresource(layer, 0);

                // Adjust stride between layers to account for smaller region in 3d texture.
                textureLayout.slicePitch = textureLayout.rowPitch * copySize.height / data.formatInfo.blockHeight;

                checkRegionsEqual(
                    bufferData,
                    textureLayout,
                    {0, 0, 0},
                    subresource.data.get(),
                    subresource.layout,
                    offset,
                    copySize
                );

                bufferData += textureLayout.sizeInBytes;
            }

            REQUIRE_CALL(device->unmapBuffer(buffer));
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-to-buffer-offset-mip1", D3D12 | Vulkan | Metal | WGPU | CUDA)
{
    TextureTestOptions options(device);
    options.addVariants(
        TTShape::All,        // all shapes
        TTArray::Both,       // array and non-array
        TTMip::On,           // with mips
        TTMS::Off,           // without multisampling
        TTPowerOf2::Both,    // test both power-of-2 and non-power-of-2 sizes where possible
        TTFmtStencil::Off,   // no stencil formats
        TTFmtCompressed::Off // no compressed formats (mip calculations are painful with them!)
    );

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();
            uint64_t totalSize = 0;
            ComPtr<IBuffer> buffer;
            REQUIRE_CALL(getSizeAndMakeBuffer(c, &totalSize, buffer.writeRef()));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Horrible but only way to zero out the read-back buffer is to copy to it from another
            // cleared buffer!
            ComPtr<IBuffer> zeroBuffer;
            BufferDesc zeroBufferDesc;
            zeroBufferDesc.size = totalSize;
            zeroBufferDesc.usage =
                BufferUsage::CopySource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination;
            device->createBuffer(zeroBufferDesc, nullptr, zeroBuffer.writeRef());
            commandEncoder->clearBuffer(zeroBuffer);
            commandEncoder->copyBuffer(buffer, 0, zeroBuffer, 0, totalSize);

            // Pick offset for the extra copy.
            Extent3D size = calcMipSize(data.desc.size, 1);
            Offset3D offset = {size.width / 4, size.height / 4, size.depth / 4};
            offset.x = math::calcAligned2(offset.x, data.formatInfo.blockWidth);
            offset.y = math::calcAligned2(offset.y, data.formatInfo.blockHeight);

            // Copy region of 2nd texture
            uint64_t bufferOffset = 0;
            for (uint32_t layer = 0; layer < data.desc.getLayerCount(); layer++)
            {
                SubresourceLayout textureLayout;
                REQUIRE_CALL(c->getTexture()->getSubresourceLayout(1, &textureLayout));

                commandEncoder->copyTextureToBuffer(
                    buffer,
                    bufferOffset,
                    textureLayout.sizeInBytes,
                    textureLayout.rowPitch,
                    c->getTexture(),
                    layer,
                    1,
                    offset,
                    Extent3D::kWholeTexture
                );

                bufferOffset += textureLayout.sizeInBytes;
            }
            queue->submit(commandEncoder->finish());

            queue->waitOnHost();

            // Verify buffer contents match texture data
            void* bufferData_ = nullptr;
            REQUIRE_CALL(device->mapBuffer(buffer, CpuAccessMode::Read, &bufferData_));
            uint8_t* bufferData = (uint8_t*)bufferData_;

            Extent3D checkExtents = {size.width - offset.x, size.height - offset.y, size.depth - offset.z};

            for (uint32_t layer = 0; layer < data.desc.getLayerCount(); layer++)
            {
                SubresourceLayout textureLayout;
                REQUIRE_CALL(c->getTexture()->getSubresourceLayout(1, &textureLayout));

                const TextureData::Subresource& subresource = data.getSubresource(layer, 1);

                // Adjust stride between layers to account for smaller region in 3d texture.
                textureLayout.slicePitch = textureLayout.rowPitch * checkExtents.height / data.formatInfo.blockHeight;

                checkRegionsEqual(
                    bufferData,
                    textureLayout,
                    {0, 0, 0},
                    subresource.data.get(),
                    subresource.layout,
                    offset,
                    checkExtents
                );

                bufferData += textureLayout.sizeInBytes;
            }

            REQUIRE_CALL(device->unmapBuffer(buffer));
        }
    );
}

GPU_TEST_CASE("cmd-copy-texture-to-buffer-sizeoffset-mip1", D3D12 | Vulkan | Metal | WGPU | CUDA)
{
    TextureTestOptions options(device);
    options.addVariants(
        TTShape::All,        // all shapes
        TTArray::Both,       // array and non-array
        TTMip::On,           // with mips
        TTMS::Off,           // without multisampling
        TTPowerOf2::Both,    // test both power-of-2 and non-power-of-2 sizes where possible
        TTFmtStencil::Off,   // no stencil formats
        TTFmtCompressed::Off // no compressed formats (mip calculations are painful with them!)
    );

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();
            uint64_t totalSize = 0;
            ComPtr<IBuffer> buffer;
            REQUIRE_CALL(getSizeAndMakeBuffer(c, &totalSize, buffer.writeRef()));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Horrible but only way to zero out the read-back buffer is to copy to it from another
            // cleared buffer!
            ComPtr<IBuffer> zeroBuffer;
            BufferDesc zeroBufferDesc;
            zeroBufferDesc.size = totalSize;
            zeroBufferDesc.usage =
                BufferUsage::CopySource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination;
            device->createBuffer(zeroBufferDesc, nullptr, zeroBuffer.writeRef());
            commandEncoder->clearBuffer(zeroBuffer);
            commandEncoder->copyBuffer(buffer, 0, zeroBuffer, 0, totalSize);

            // Pick offset for the extra copy.
            Extent3D size = calcMipSize(data.desc.size, 1);
            Offset3D offset = {size.width / 4, size.height / 4, size.depth / 4};
            offset.x = math::calcAligned2(offset.x, data.formatInfo.blockWidth);
            offset.y = math::calcAligned2(offset.y, data.formatInfo.blockHeight);

            Extent3D copySize = {max(size.width / 2, 1u), max(size.height / 2, 1u), max(size.depth / 2, 1u)};
            copySize.width = math::calcAligned2(copySize.width, data.formatInfo.blockWidth);
            copySize.height = math::calcAligned2(copySize.height, data.formatInfo.blockHeight);

            // Copy region of 2nd texture
            uint64_t bufferOffset = 0;
            for (uint32_t layer = 0; layer < data.desc.getLayerCount(); layer++)
            {
                SubresourceLayout textureLayout;
                REQUIRE_CALL(c->getTexture()->getSubresourceLayout(1, &textureLayout));

                commandEncoder->copyTextureToBuffer(
                    buffer,
                    bufferOffset,
                    textureLayout.sizeInBytes,
                    textureLayout.rowPitch,
                    c->getTexture(),
                    layer,
                    1,
                    offset,
                    copySize
                );

                bufferOffset += textureLayout.sizeInBytes;
            }
            queue->submit(commandEncoder->finish());

            queue->waitOnHost();

            // Verify buffer contents match texture data
            void* bufferData_ = nullptr;
            REQUIRE_CALL(device->mapBuffer(buffer, CpuAccessMode::Read, &bufferData_));
            uint8_t* bufferData = (uint8_t*)bufferData_;

            for (uint32_t layer = 0; layer < data.desc.getLayerCount(); layer++)
            {
                SubresourceLayout textureLayout;
                REQUIRE_CALL(c->getTexture()->getSubresourceLayout(1, &textureLayout));

                const TextureData::Subresource& subresource = data.getSubresource(layer, 1);

                // Adjust stride between layers to account for smaller region in 3d texture.
                textureLayout.slicePitch = textureLayout.rowPitch * copySize.height / data.formatInfo.blockHeight;

                checkRegionsEqual(
                    bufferData,
                    textureLayout,
                    {0, 0, 0},
                    subresource.data.get(),
                    subresource.layout,
                    offset,
                    copySize
                );

                bufferData += textureLayout.sizeInBytes;
            }

            REQUIRE_CALL(device->unmapBuffer(buffer));
        }
    );
}
