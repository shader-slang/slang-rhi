#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class TextureImpl : public Texture
{
public:
    WGPUTexture m_texture = nullptr;

    TextureImpl(Device* device, const TextureDesc& desc);
    ~TextureImpl();

    // ITexture implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;

    Result getSubresourceRegionLayout(
        uint32_t mipLevel,
        uint32_t layerIndex,
        Offset3D offset,
        Extents extents,
        SubresourceLayout* outLayout
    ) override;
};

class TextureViewImpl : public TextureView
{
public:
    RefPtr<TextureImpl> m_texture;
    WGPUTextureView m_textureView = nullptr;

    TextureViewImpl(Device* device, const TextureViewDesc& desc);
    ~TextureViewImpl();

    // ITextureView implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::wgpu
