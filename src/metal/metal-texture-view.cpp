#include "metal-texture-view.h"

namespace rhi::metal {

Result TextureViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLTexture;
    outHandle->value = (uint64_t)m_textureView.get();
    return SLANG_OK;
}

} // namespace rhi::metal
