#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class TextureImpl : public Texture
{
public:
    DeviceImpl* m_device;
    WGPUTexture m_texture = nullptr;

    TextureImpl(DeviceImpl* device, const TextureDesc& desc);
    ~TextureImpl();

    // ITexture implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
};

class TextureViewImpl : public TextureView
{
public:
    DeviceImpl* m_device;
    RefPtr<TextureImpl> m_texture;
    WGPUTextureView m_textureView = nullptr;

    TextureViewImpl(DeviceImpl* device, const TextureViewDesc& desc);
    ~TextureViewImpl();

    // ITextureView implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::wgpu
