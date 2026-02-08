#pragma once

#include "metal-base.h"

namespace rhi::metal {

class TextureImpl : public Texture
{
public:
    TextureImpl(Device* device, const TextureDesc& desc);
    ~TextureImpl();

    virtual void deleteThis() override;

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // ITexture implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDefaultView(ITextureView** outTextureView) override;

public:
    NS::SharedPtr<MTL::Texture> m_texture;
    MTL::TextureType m_textureType;
    MTL::PixelFormat m_pixelFormat;
    RefPtr<TextureViewImpl> m_defaultView;
};

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(Device* device, const TextureViewDesc& desc);

    // RefObject implementation
    virtual void makeExternal() override { m_texture.establishStrongReference(); }
    virtual void makeInternal() override { m_texture.breakStrongReference(); }

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // ITextureView implementation
    virtual SLANG_NO_THROW ITexture* SLANG_MCALL getTexture() override { return m_texture; }

public:
    BreakableReference<TextureImpl> m_texture;
    NS::SharedPtr<MTL::Texture> m_textureView;
};

} // namespace rhi::metal
