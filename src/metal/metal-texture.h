#pragma once

#include "metal-base.h"
#include "metal-device.h"

namespace rhi::metal {

class TextureImpl : public Texture
{
public:
    typedef Texture Parent;

    TextureImpl(const TextureDesc& desc, DeviceImpl* device);
    ~TextureImpl();

    BreakableReference<DeviceImpl> m_device;
    NS::SharedPtr<MTL::Texture> m_texture;
    MTL::TextureType m_textureType;
    MTL::PixelFormat m_pixelFormat;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeResourceHandle(InteropHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(InteropHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL setDebugName(const char* name) override;
};

} // namespace rhi::metal
