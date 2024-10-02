#pragma once

#include "metal-base.h"
#include "metal-buffer.h"
#include "metal-device.h"
#include "metal-texture.h"

namespace rhi::metal {

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(const TextureViewDesc& desc)
        : TextureView(desc)
    {
    }

    RefPtr<TextureImpl> m_texture;
    NS::SharedPtr<MTL::Texture> m_textureView;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::metal
