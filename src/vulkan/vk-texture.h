#pragma once

#include "vk-base.h"
#include "vk-device.h"

namespace rhi::vk {

class TextureImpl : public Texture
{
public:
    typedef Texture Parent;
    TextureImpl(const TextureDesc& desc, DeviceImpl* device);
    ~TextureImpl();

    VkImage m_image = VK_NULL_HANDLE;
    VkFormat m_vkformat = VK_FORMAT_R8G8B8A8_UNORM;
    VkDeviceMemory m_imageMemory = VK_NULL_HANDLE;
    bool m_isWeakImageReference = false;
    RefPtr<DeviceImpl> m_device;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeResourceHandle(InteropHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(InteropHandle* outHandle) override;
};

} // namespace rhi::vk
