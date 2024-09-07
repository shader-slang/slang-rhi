#include "metal-texture.h"
#include "metal-util.h"

namespace rhi::metal {

TextureImpl::TextureImpl(const TextureDesc& desc, DeviceImpl* device)
    : Parent(desc)
    , m_device(device)
{
}

TextureImpl::~TextureImpl() {}

Result TextureImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLTexture;
    outHandle->value = (uint64_t)m_texture.get();
    return SLANG_OK;
}

Result TextureImpl::getSharedHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

} // namespace rhi::metal
