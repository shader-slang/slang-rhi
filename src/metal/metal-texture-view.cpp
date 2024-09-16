#include "metal-texture-view.h"

namespace rhi::metal {

Result TextureViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLTexture;
    outHandle->value = (uint64_t)m_textureView.get();
    return SLANG_OK;
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
