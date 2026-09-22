#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class TextureImpl : public Texture
{
public:
    TextureImpl(Device* device, const TextureDesc& desc);
    ~TextureImpl();

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // ITexture implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDefaultView(ITextureView** outTextureView) override;

public:
    WGPUTexture m_texture = nullptr;
    RefPtr<TextureViewImpl> m_defaultView;
};

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(Device* device, const TextureViewDesc& desc);
    ~TextureViewImpl();

    // RefObject implementation
    // A texture view holds a breakable reference to its texture in addition to the one to the
    // device that `DeviceChild` manages, because a texture owns its default view; both cycles
    // have to be broken once the view is only referenced internally.
    virtual void makeExternal() override
    {
        DeviceChild::makeExternal();
        m_texture.establishStrongReference();
    }
    virtual void makeInternal() override
    {
        DeviceChild::makeInternal();
        m_texture.breakStrongReference();
    }

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // ITextureView implementation
    virtual SLANG_NO_THROW ITexture* SLANG_MCALL getTexture() override { return m_texture; }

public:
    BreakableReference<TextureImpl> m_texture;
    WGPUTextureView m_textureView = nullptr;
};

} // namespace rhi::wgpu
