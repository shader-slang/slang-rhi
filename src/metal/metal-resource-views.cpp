#include "metal-resource-views.h"

namespace rhi::metal {

TextureViewImpl::~TextureViewImpl() {}

Result TextureViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLTexture;
    outHandle->value = (uint64_t)m_textureView.get();
    return SLANG_OK;
}

BufferViewImpl::~BufferViewImpl() {}

Result BufferViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    return m_buffer->getNativeHandle(outHandle);
}

TexelBufferViewImpl::TexelBufferViewImpl(DeviceImpl* device)
    : ResourceViewImpl(ViewType::TexelBuffer, device)
{
}

TexelBufferViewImpl::~TexelBufferViewImpl() {}

Result TexelBufferViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    return SLANG_E_NOT_AVAILABLE;
}

DeviceAddress AccelerationStructureImpl::getDeviceAddress()
{
    return 0;
}

Result AccelerationStructureImpl::getNativeHandle(NativeHandle* outHandle)
{
    return SLANG_E_NOT_AVAILABLE;
}

AccelerationStructureImpl::~AccelerationStructureImpl() {}

} // namespace rhi::metal
