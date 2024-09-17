#pragma once

#include "metal-base.h"
#include "metal-buffer.h"
#include "metal-device.h"
#include "metal-texture.h"

namespace rhi::metal {

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(RendererBase* device, const TextureViewDesc& desc)
        : TextureView(device, desc)
    {
    }

    RefPtr<TextureImpl> m_texture;
    NS::SharedPtr<MTL::Texture> m_textureView;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class AccelerationStructureImpl : public AccelerationStructure
{
public:
    RefPtr<BufferImpl> m_buffer;
    RefPtr<DeviceImpl> m_device;

public:
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    ~AccelerationStructureImpl();
};

} // namespace rhi::metal
