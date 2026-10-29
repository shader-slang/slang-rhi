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

public:
    WGPUTexture m_texture = nullptr;
};

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(TextureImpl* texture, const TextureViewDesc& desc);
    ~TextureViewImpl();

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // ITextureView implementation
    virtual SLANG_NO_THROW ITexture* SLANG_MCALL getTexture() override { return m_texture; }

public:
    // Immutable borrowed association; TextureView pairs each consumer reference with a texture reference.
    TextureImpl* m_texture;
    WGPUTextureView m_textureView = nullptr;
};

} // namespace rhi::wgpu
