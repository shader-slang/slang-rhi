#include "cpu-buffer.h"
#include "cpu-device.h"
#include "cpu-texture.h"

namespace rhi::cpu {

BufferImpl::BufferImpl(Device* device, const BufferDesc& desc)
    : Buffer(device, desc)
{
}

BufferImpl::~BufferImpl()
{
    if (m_data)
    {
        std::free(m_data);
    }
}

DeviceAddress BufferImpl::getDeviceAddress()
{
    return (DeviceAddress)m_data;
}

Result DeviceImpl::createBuffer(const BufferDesc& desc_, const void* initData, IBuffer** outBuffer)
{
    BufferDesc desc = fixupBufferDesc(desc_);
    RefPtr<BufferImpl> buffer = new BufferImpl(this, desc);
    buffer->m_data = (uint8_t*)std::malloc(desc.size);
    if (!buffer->m_data)
    {
        return SLANG_E_OUT_OF_MEMORY;
    }
    if (initData)
    {
        std::memcpy(buffer->m_data, initData, desc.size);
    }
    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData)
{
    *outData = checked_cast<BufferImpl*>(buffer)->m_data;
    return SLANG_OK;
}

Result DeviceImpl::unmapBuffer(IBuffer* buffer)
{
    SLANG_UNUSED(buffer);
    return SLANG_OK;
}

Result DeviceImpl::readTexture(
    ITexture* texture,
    uint32_t layer,
    uint32_t mipLevel,
    ISlangBlob** outBlob,
    Size* outRowPitch,
    Size* outPixelSize
)
{
    auto textureImpl = checked_cast<TextureImpl*>(texture);

    // Calculate layout info.
    SubresourceLayout layout;
    SLANG_RETURN_ON_FAIL(texture->getSubresourceLayout(mipLevel, &layout));

    // Create blob for result.
    auto blob = OwnedBlob::create(layout.sizeInBytes);

    // Get src + dest buffers.
    uint8_t* srcBuffer = (uint8_t*)textureImpl->m_data;
    uint8_t* dstBuffer = (uint8_t*)blob->getBufferPointer();

    // Should be able to make assumption that subresource layout info
    // matches those stored in the mip. If they don't match, this is a bug.
    TextureImpl::MipLevel mipLevelInfo = textureImpl->m_mipLevels[mipLevel];
    SLANG_RHI_ASSERT(mipLevelInfo.extents[0] == layout.size.width);
    SLANG_RHI_ASSERT(mipLevelInfo.extents[1] == layout.size.height);
    SLANG_RHI_ASSERT(mipLevelInfo.extents[2] == layout.size.depth);
    SLANG_RHI_ASSERT(mipLevelInfo.strides[1] == layout.strideY);
    SLANG_RHI_ASSERT(mipLevelInfo.strides[2] == layout.strideZ);

    // Step forward to the mip data in the texture.
    srcBuffer += mipLevelInfo.offset;

    // Copy a row at a time.
    for (int z = 0; z < layout.size.depth; z++)
    {
        uint8_t* srcRow = srcBuffer;
        uint8_t* dstRow = dstBuffer;
        for (int y = 0; y < layout.rowCount; y++)
        {
            std::memcpy(dstRow, srcRow, layout.strideY);
            srcRow += layout.strideY;
            dstRow += layout.strideY;
        }
        srcBuffer += layout.strideZ;
        dstBuffer += layout.strideZ;
    }

    // Return data.
    returnComPtr(outBlob, blob);
    if (outRowPitch)
        *outRowPitch = layout.strideY;
    if (outPixelSize)
        *outPixelSize = layout.sizeInBytes / layout.size.width;
    return SLANG_OK;
}


Result DeviceImpl::readBuffer(IBuffer* buffer, Offset offset, Size size, ISlangBlob** outBlob)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    auto blob = OwnedBlob::create(size);
    if (offset + size > bufferImpl->m_desc.size)
    {
        return SLANG_FAIL;
    }
    std::memcpy((void*)blob->getBufferPointer(), bufferImpl->m_data + offset, size);
    returnComPtr(outBlob, blob);
    return SLANG_OK;
}

} // namespace rhi::cpu
