#include "metal-texture.h"
#include "metal-util.h"

namespace rhi::metal {

TextureImpl::TextureImpl(const Desc& desc, DeviceImpl* device)
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

Result TextureImpl::setDebugName(const char* name)
{
    Parent::setDebugName(name);
    m_texture->setLabel(MetalUtil::createString(name).get());
    return SLANG_OK;
}

} // namespace rhi::metal
