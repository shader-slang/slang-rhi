#include "vk-texture.h"
#include "vk-device.h"
#include "vk-util.h"
#include "vk-helper-functions.h"

namespace rhi::vk {

TextureImpl::TextureImpl(DeviceImpl* device, const TextureDesc& desc)
    : Texture(desc)
    , m_device(device)
{
}

TextureImpl::~TextureImpl()
{
    auto& api = m_device->m_api;
    for (auto& view : m_views)
    {
        api.vkDestroyImageView(api.m_device, view.second.imageView, nullptr);
    }
    if (!m_isWeakImageReference)
    {
        api.vkFreeMemory(api.m_device, m_imageMemory, nullptr);
        api.vkDestroyImage(api.m_device, m_image, nullptr);
    }
    if (m_sharedHandle)
    {
#if SLANG_WINDOWS_FAMILY
        CloseHandle((HANDLE)m_sharedHandle.value);
#endif
    }
}

Result TextureImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkImage;
    outHandle->value = (uint64_t)m_image;
    return SLANG_OK;
}

Result TextureImpl::getSharedHandle(NativeHandle* outHandle)
{
    // Check if a shared handle already exists for this resource.
    if (m_sharedHandle)
    {
        *outHandle = m_sharedHandle;
        return SLANG_OK;
    }

    // If a shared handle doesn't exist, create one and store it.
#if SLANG_WINDOWS_FAMILY
    VkMemoryGetWin32HandleInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    info.pNext = nullptr;
    info.memory = m_imageMemory;
    info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    auto& api = m_device->m_api;
    PFN_vkGetMemoryWin32HandleKHR vkCreateSharedHandle;
    vkCreateSharedHandle = api.vkGetMemoryWin32HandleKHR;
    if (!vkCreateSharedHandle)
    {
        return SLANG_FAIL;
    }
    SLANG_RETURN_ON_FAIL(vkCreateSharedHandle(m_device->m_device, &info, (HANDLE*)&m_sharedHandle.value) != VK_SUCCESS);
    m_sharedHandle.type = NativeHandleType::Win32;
#else
    VkMemoryGetFdInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    info.pNext = nullptr;
    info.memory = m_imageMemory;
    info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    auto& api = m_device->m_api;
    PFN_vkGetMemoryFdKHR vkCreateSharedHandle;
    vkCreateSharedHandle = api.vkGetMemoryFdKHR;
    if (!vkCreateSharedHandle)
    {
        return SLANG_FAIL;
    }
    SLANG_RETURN_ON_FAIL(vkCreateSharedHandle(m_device->m_device, &info, (int*)&m_sharedHandle.value) != VK_SUCCESS);
    m_sharedHandle.type = NativeHandleType::FileDescriptor;
#endif
    *outHandle = m_sharedHandle;
    return SLANG_OK;
}

TextureSubresourceView TextureImpl::getView(Format format, TextureAspect aspect, const SubresourceRange& range)
{
    ViewKey key = {format, aspect, range};
    TextureSubresourceView& view = m_views[key];
    if (view.imageView)
        return view;

    bool isArray = m_desc.arrayLength > 1;
    VkImageViewCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.flags = 0;
    createInfo.format = getFormatInfo(format).isTypeless ? VulkanUtil::getVkFormat(format) : m_vkformat;
    createInfo.image = m_image;
    createInfo.components = VkComponentMapping{
        VK_COMPONENT_SWIZZLE_R,
        VK_COMPONENT_SWIZZLE_G,
        VK_COMPONENT_SWIZZLE_B,
        VK_COMPONENT_SWIZZLE_A
    };
    switch (m_desc.type)
    {
    case TextureType::Texture1D:
        createInfo.viewType = isArray ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
        break;
    case TextureType::Texture2D:
        createInfo.viewType = isArray ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        break;
    case TextureType::Texture3D:
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
        break;
    case TextureType::TextureCube:
        createInfo.viewType = isArray ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
        break;
    }

    createInfo.subresourceRange.aspectMask = getAspectMaskFromFormat(m_vkformat, aspect);

    createInfo.subresourceRange.baseArrayLayer = range.baseArrayLayer;
    createInfo.subresourceRange.baseMipLevel = range.mipLevel;
    createInfo.subresourceRange.layerCount = range.layerCount;
    createInfo.subresourceRange.levelCount = range.mipLevelCount;

    VkResult result =
        m_device->m_api.vkCreateImageView(m_device->m_api.m_device, &createInfo, nullptr, &view.imageView);
    SLANG_RHI_ASSERT(result == VK_SUCCESS);
    return view;
}

} // namespace rhi::vk
