#pragma once

#include "metal-base.h"

namespace rhi::metal {

class TextureImpl : public Texture
{
public:
    TextureImpl(Device* device, const TextureDesc& desc);
    ~TextureImpl();

    NS::SharedPtr<MTL::Texture> m_texture;
    MTL::TextureType m_textureType;
    MTL::PixelFormat m_pixelFormat;

    RefPtr<TextureViewImpl> m_defaultView;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDefaultView(ITextureView** outTextureView) override;
};

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(Device* device, const TextureViewDesc& desc);

    virtual void makeExternal() override { m_texture.establishStrongReference(); }
    virtual void makeInternal() override { m_texture.breakStrongReference(); }

    BreakableReference<TextureImpl> m_texture;
    NS::SharedPtr<MTL::Texture> m_textureView;

    // ITextureView implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW ITexture* SLANG_MCALL getTexture() override { return m_texture; }
};

} // namespace rhi::metal
