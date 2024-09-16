#pragma once

#include "metal-base.h"
#include "metal-device.h"

namespace rhi::metal {

class TextureImpl : public Texture
{
public:
    TextureImpl(const TextureDesc& desc, DeviceImpl* device);
    ~TextureImpl();

    BreakableReference<DeviceImpl> m_device;
    NS::SharedPtr<MTL::Texture> m_texture;
    MTL::TextureType m_textureType;
    MTL::PixelFormat m_pixelFormat;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::metal
