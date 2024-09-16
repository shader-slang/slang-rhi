#pragma once

#include "vk-base.h"
#include "vk-buffer.h"
#include "vk-device.h"
#include "vk-texture.h"

namespace rhi::vk {

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(RendererBase* device, const TextureViewDesc& desc)
        : TextureView(device, desc)
    {
    }

    RefPtr<TextureImpl> m_texture;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    TextureSubresourceView getView();
};

class AccelerationStructureImpl : public AccelerationStructureBase
{
public:
    VkAccelerationStructureKHR m_vkHandle = VK_NULL_HANDLE;
    RefPtr<BufferImpl> m_buffer;
    VkDeviceSize m_offset;
    VkDeviceSize m_size;
    RefPtr<DeviceImpl> m_device;

public:
    AccelerationStructureImpl(RendererBase* device, const IAccelerationStructure::CreateDesc& desc)
        : AccelerationStructureBase(device, desc)
    {
    }

    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    ~AccelerationStructureImpl();
};

} // namespace rhi::vk
