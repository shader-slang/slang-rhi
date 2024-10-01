#include "vk-texture-view.h"

namespace rhi::vk {

Result TextureViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    return SLANG_E_NOT_AVAILABLE;
}

TextureSubresourceView TextureViewImpl::getView()
{
    return m_texture->getView(m_desc.format, m_desc.aspect, m_desc.subresourceRange);
}

} // namespace rhi::vk
