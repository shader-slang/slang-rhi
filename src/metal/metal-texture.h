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
    // True if this texture is created from a swap chain buffer.
    // Swap chain textures are deleted immediately when deleteThis() is called.
    bool m_isSwapchainTexture = false;
    RefPtr<TextureViewImpl> m_defaultView;
};

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(Device* device, const TextureViewDesc& desc);

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
    NS::SharedPtr<MTL::Texture> m_textureView;
};

} // namespace rhi::metal
