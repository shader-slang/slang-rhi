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

public:
    NS::SharedPtr<MTL::Texture> m_texture;
    MTL::TextureType m_textureType;
    MTL::PixelFormat m_pixelFormat;
    // True if this texture is created from a swap chain buffer.
    // Swap chain textures are deleted immediately when deleteThis() is called.
    bool m_isSwapchainTexture = false;
};

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(TextureImpl* texture, const TextureViewDesc& desc);

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // ITextureView implementation
    virtual SLANG_NO_THROW ITexture* SLANG_MCALL getTexture() override { return m_texture; }

public:
    // Immutable borrowed association; TextureView pairs each consumer reference with a texture reference.
    TextureImpl* m_texture;
    NS::SharedPtr<MTL::Texture> m_textureView;
};

} // namespace rhi::metal
