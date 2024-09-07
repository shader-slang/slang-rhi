#include "vk-texture.h"

namespace rhi::vk {

TextureImpl::TextureImpl(const TextureDesc& desc, DeviceImpl* device)
    : Parent(desc)
    , m_device(device)
{
}

TextureImpl::~TextureImpl()
{
    auto& vkAPI = m_device->m_api;
    if (!m_isWeakImageReference)
    {
        vkAPI.vkFreeMemory(vkAPI.m_device, m_imageMemory, nullptr);
        vkAPI.vkDestroyImage(vkAPI.m_device, m_image, nullptr);
    }
    if (sharedHandle)
    {
#if SLANG_WINDOWS_FAMILY
        CloseHandle((HANDLE)sharedHandle.value);
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
    if (sharedHandle)
    {
        *outHandle = sharedHandle;
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
    SLANG_RETURN_ON_FAIL(vkCreateSharedHandle(m_device->m_device, &info, (HANDLE*)&sharedHandle.value) != VK_SUCCESS);
    sharedHandle.type = NativeHandleType::Win32;
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
    SLANG_RETURN_ON_FAIL(vkCreateSharedHandle(m_device->m_device, &info, (int*)&sharedHandle.value) != VK_SUCCESS);
    sharedHandle.type = NativeHandleType::FileDescriptor;
#endif
    *outHandle = sharedHandle;
    return SLANG_OK;
}

} // namespace rhi::vk
