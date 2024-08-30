#include "metal-resource-views.h"

namespace rhi::metal {

TextureResourceViewImpl::~TextureResourceViewImpl() {}

Result TextureResourceViewImpl::getNativeHandle(InteropHandle* outHandle)
{
    outHandle->api = InteropHandleAPI::Metal;
    outHandle->handleValue = reinterpret_cast<uintptr_t>(m_textureView.get());
    return SLANG_OK;
}

BufferViewImpl::~BufferViewImpl() {}

Result BufferViewImpl::getNativeHandle(InteropHandle* outHandle)
{
    outHandle->api = InteropHandleAPI::Metal;
    outHandle->handleValue = reinterpret_cast<uintptr_t>(m_buffer->m_buffer.get());
    return SLANG_OK;
}

TexelBufferViewImpl::TexelBufferViewImpl(DeviceImpl* device)
    : ResourceViewImpl(ViewType::TexelBuffer, device)
{
}

TexelBufferViewImpl::~TexelBufferViewImpl() {}

Result TexelBufferViewImpl::getNativeHandle(InteropHandle* outHandle)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

DeviceAddress AccelerationStructureImpl::getDeviceAddress()
{
    return 0;
}

Result AccelerationStructureImpl::getNativeHandle(InteropHandle* outHandle)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

AccelerationStructureImpl::~AccelerationStructureImpl() {}

} // namespace rhi::metal
