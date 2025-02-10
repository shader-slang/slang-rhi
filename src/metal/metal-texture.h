#pragma once

#include "metal-base.h"

namespace rhi::metal {

class TextureImpl : public Texture
{
public:
    TextureImpl(const TextureDesc& desc);
    ~TextureImpl();

    NS::SharedPtr<MTL::Texture> m_texture;
    MTL::TextureType m_textureType;
    MTL::PixelFormat m_pixelFormat;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
};

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
