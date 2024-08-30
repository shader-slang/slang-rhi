#pragma once

#include "vk-base.h"
#include "vk-buffer.h"
#include "vk-device.h"
#include "vk-texture.h"

namespace rhi::vk {

class ResourceViewImpl : public ResourceViewBase
{
public:
    enum class ViewType
    {
        Texture,
        TexelBuffer,
        PlainBuffer,
    };

public:
    ResourceViewImpl(ViewType viewType, DeviceImpl* device)
        : m_type(viewType)
        , m_device(device)
    {
    }
    ViewType m_type;
    RefPtr<DeviceImpl> m_device;
};

class TextureViewImpl : public ResourceViewImpl
{
public:
    TextureViewImpl(DeviceImpl* device)
        : ResourceViewImpl(ViewType::Texture, device)
    {
    }
    ~TextureViewImpl();
    RefPtr<TextureImpl> m_texture;
    VkImageView m_view;
    VkImageLayout m_layout;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(InteropHandle* outHandle) override;
};

class TexelBufferViewImpl : public ResourceViewImpl
{
public:
    TexelBufferViewImpl(DeviceImpl* device);
    ~TexelBufferViewImpl();
    RefPtr<BufferImpl> m_buffer;
    VkBufferView m_view;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(InteropHandle* outHandle) override;
};

class PlainBufferViewImpl : public ResourceViewImpl
{
public:
    PlainBufferViewImpl(DeviceImpl* device);
    RefPtr<BufferImpl> m_buffer;
    VkDeviceSize offset;
    VkDeviceSize size;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(InteropHandle* outHandle) override;
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
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(InteropHandle* outHandle) override;
    ~AccelerationStructureImpl();
};

} // namespace rhi::vk
