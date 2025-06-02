#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class TextureImpl : public Texture
{
public:
    WGPUTexture m_texture = nullptr;
    RefPtr<TextureViewImpl> m_defaultView;

    TextureImpl(Device* device, const TextureDesc& desc);
    ~TextureImpl();

    // ITexture implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDefaultView(ITextureView** outTextureView) override;
};

class TextureViewImpl : public TextureView
{
public:
    BreakableReference<TextureImpl> m_texture;
    WGPUTextureView m_textureView = nullptr;

    TextureViewImpl(Device* device, const TextureViewDesc& desc);
    ~TextureViewImpl();

    virtual void makeExternal() override { m_texture.establishStrongReference(); }
    virtual void makeInternal() override { m_texture.breakStrongReference(); }

    // ITextureView implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW ITexture* SLANG_MCALL getTexture() override { return m_texture; }
};

} // namespace rhi::wgpu
