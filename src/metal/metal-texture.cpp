#include "metal-texture.h"
#include "metal-util.h"

namespace rhi::metal {

TextureImpl::TextureImpl(const TextureDesc& desc, DeviceImpl* device)
    : Parent(desc)
    , m_device(device)
{
}

TextureImpl::~TextureImpl() {}

Result TextureImpl::getNativeResourceHandle(InteropHandle* outHandle)
{
    outHandle->api = InteropHandleAPI::Metal;
    outHandle->handleValue = reinterpret_cast<intptr_t>(m_texture.get());
    return SLANG_OK;
}

Result TextureImpl::getSharedHandle(InteropHandle* outHandle)
{
    return SLANG_E_NOT_AVAILABLE;
}

} // namespace rhi::metal
