#include "vk-surface.h"
#include "vk-device.h"
#include "vk-texture.h"
#include "vk-util.h"
#include "vk-helper-functions.h"
#include "cocoa-util.h"

#include "core/static_vector.h"

#include <vector>

namespace rhi::vk {

#if 0

Index SwapchainImpl::_indexOfFormat(std::vector<VkSurfaceFormatKHR>& formatsIn, VkFormat format)
{
    const Index numFormats = formatsIn.size();
    const VkSurfaceFormatKHR* formats = formatsIn.data();

    for (Index i = 0; i < numFormats; ++i)
    {
        if (formats[i].format == format)
        {
            return i;
        }
    }
    return -1;
}


#endif

SurfaceImpl::~SurfaceImpl()
{
    auto& api = m_device->m_api;

    destroySwapchain();

    if (m_surface)
    {
        api.vkDestroySurfaceKHR(api.m_instance, m_surface, nullptr);
    }

    if (m_nextImageSemaphore)
    {
        api.vkDestroySemaphore(api.m_device, m_nextImageSemaphore, nullptr);
    }

#if SLANG_APPLE_FAMILY
    CocoaUtil::destroyMetalLayer(m_metalLayer);
#endif
}

Result SurfaceImpl::init(DeviceImpl* device, WindowHandle windowHandle)
{
    m_device = device;
    m_windowHandle = windowHandle;

    auto& api = m_device->m_api;

    VkSemaphoreCreateInfo semaphoreCreateInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    SLANG_VK_RETURN_ON_FAIL(api.vkCreateSemaphore(api.m_device, &semaphoreCreateInfo, nullptr, &m_nextImageSemaphore));

#if SLANG_WINDOWS_FAMILY
    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.hinstance = ::GetModuleHandle(nullptr);
    surfaceCreateInfo.hwnd = (HWND)windowHandle.handleValues[0];
    SLANG_VK_RETURN_ON_FAIL(api.vkCreateWin32SurfaceKHR(api.m_instance, &surfaceCreateInfo, nullptr, &m_surface));
#elif SLANG_APPLE_FAMILY
    m_metalLayer = CocoaUtil::createMetalLayer((void*)windowHandle.handleValues[0]);
    VkMetalSurfaceCreateInfoEXT surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    surfaceCreateInfo.pLayer = (CAMetalLayer*)m_metalLayer;
    SLANG_VK_RETURN_ON_FAIL(api.vkCreateMetalSurfaceEXT(api.m_instance, &surfaceCreateInfo, nullptr, &m_surface));
#elif SLANG_ENABLE_XLIB
    VkXlibSurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.dpy = (Display*)windowHandle.handleValues[0];
    surfaceCreateInfo.window = (Window)windowHandle.handleValues[1];
    SLANG_VK_RETURN_ON_FAIL(api.vkCreateXlibSurfaceKHR(api.m_instance, &surfaceCreateInfo, nullptr, &m_surface));
#else
    return SLANG_E_NOT_AVAILABLE;
#endif

    VkBool32 supported = false;
    api.vkGetPhysicalDeviceSurfaceSupportKHR(api.m_physicalDevice, m_device->m_queueFamilyIndex, m_surface, &supported);
    if (!supported)
    {
        return SLANG_FAIL;
    }

    uint32_t formatCount = 0;
    api.vkGetPhysicalDeviceSurfaceFormatsKHR(api.m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    api.vkGetPhysicalDeviceSurfaceFormatsKHR(api.m_physicalDevice, m_surface, &formatCount, surfaceFormats.data());

    for (uint32_t i = 0; i < formatCount; ++i)
    {
        switch (surfaceFormats[i].format)
        {
        case VK_FORMAT_R8G8B8A8_UNORM:
            m_supportedFormats.push_back(Format::R8G8B8A8_UNORM);
            break;
        case VK_FORMAT_R8G8B8A8_SRGB:
            m_supportedFormats.push_back(Format::R8G8B8A8_UNORM_SRGB);
            break;
        case VK_FORMAT_B8G8R8A8_UNORM:
            m_supportedFormats.push_back(Format::B8G8R8A8_UNORM);
            break;
        case VK_FORMAT_B8G8R8A8_SRGB:
            m_supportedFormats.push_back(Format::B8G8R8A8_UNORM_SRGB);
            break;
        default:
            break;
        }
    }

    m_info.preferredFormat = m_supportedFormats.front();
    m_info.supportedUsage = TextureUsage::Present | TextureUsage::RenderTarget | TextureUsage::CopyDestination;
    m_info.formats = m_supportedFormats.data();
    m_info.formatCount = (uint32_t)m_supportedFormats.size();

    return SLANG_OK;
}

Result SurfaceImpl::createSwapchain()
{
    auto& api = m_device->m_api;

    VkExtent2D imageExtent = {(uint32_t)m_config.width, (uint32_t)m_config.height};

    // It is necessary to query the caps -> otherwise the LunarG verification layer will
    // issue an error
    {
        VkSurfaceCapabilitiesKHR surfaceCaps;

        SLANG_VK_RETURN_ON_FAIL(
            api.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(api.m_physicalDevice, m_surface, &surfaceCaps)
        );
    }

    // Query available present modes.
    uint32_t presentModeCount = 0;
    api.vkGetPhysicalDeviceSurfacePresentModesKHR(api.m_physicalDevice, m_surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    api.vkGetPhysicalDeviceSurfacePresentModesKHR(
        api.m_physicalDevice,
        m_surface,
        &presentModeCount,
        presentModes.data()
    );

    // Choose present mode.
    static const VkPresentModeKHR kVsyncOffModes[] = {
        VK_PRESENT_MODE_IMMEDIATE_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR,
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_MAX_ENUM_KHR
    };
    static const VkPresentModeKHR kVsyncOnModes[] = {
        VK_PRESENT_MODE_FIFO_RELAXED_KHR,
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_IMMEDIATE_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR,
        VK_PRESENT_MODE_MAX_ENUM_KHR
    };
    const VkPresentModeKHR* checkPresentModes = m_config.vsync ? kVsyncOnModes : kVsyncOffModes;
    VkPresentModeKHR selectedPresentMode = VK_PRESENT_MODE_MAX_ENUM_KHR;
    for (int i = 0; checkPresentModes[i] != VK_PRESENT_MODE_MAX_ENUM_KHR; ++i)
    {
        if (std::find(presentModes.begin(), presentModes.end(), checkPresentModes[i]) != presentModes.end())
        {
            selectedPresentMode = checkPresentModes[i];
            break;
        }
    }
    if (selectedPresentMode == VK_PRESENT_MODE_MAX_ENUM_KHR)
    {
        return SLANG_FAIL;
    }

    VkFormat format = VulkanUtil::getVkFormat(m_config.format);
    VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE;

    VkSwapchainCreateInfoKHR swapchainDesc = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchainDesc.surface = m_surface;
    swapchainDesc.minImageCount = 3; // TODO add to config
    swapchainDesc.imageFormat = format;
    swapchainDesc.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainDesc.imageExtent = imageExtent;
    swapchainDesc.imageArrayLayers = 1;
    swapchainDesc.imageUsage = _calcImageUsageFlags(m_config.usage);
    swapchainDesc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainDesc.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchainDesc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainDesc.presentMode = selectedPresentMode;
    swapchainDesc.clipped = VK_TRUE;
    swapchainDesc.oldSwapchain = oldSwapchain;

    SLANG_VK_RETURN_ON_FAIL(api.vkCreateSwapchainKHR(api.m_device, &swapchainDesc, nullptr, &m_swapchain));

    uint32_t swapchainImageCount = 0;
    api.vkGetSwapchainImagesKHR(api.m_device, m_swapchain, &swapchainImageCount, nullptr);
    std::vector<VkImage> swapchainImages(swapchainImageCount);
    api.vkGetSwapchainImagesKHR(api.m_device, m_swapchain, &swapchainImageCount, swapchainImages.data());

    for (uint32_t i = 0; i < swapchainImageCount; i++)
    {
        TextureDesc textureDesc = {};
        textureDesc.usage = m_config.usage;
        textureDesc.type = TextureType::Texture2D;
        textureDesc.arrayLength = 1;
        textureDesc.format = m_config.format;
        textureDesc.size.width = m_config.width;
        textureDesc.size.height = m_config.height;
        textureDesc.size.depth = 1;
        textureDesc.mipLevelCount = 1;
        textureDesc.defaultState = ResourceState::Present;
        RefPtr<TextureImpl> texture = new TextureImpl(m_device, textureDesc);
        texture->m_image = swapchainImages[i];
        texture->m_imageMemory = 0;
        texture->m_vkformat = format;
        texture->m_isWeakImageReference = true;
        m_textures.push_back(texture);
    }

    return SLANG_OK;
}

void SurfaceImpl::destroySwapchain()
{
    auto& api = m_device->m_api;
    api.vkQueueWaitIdle(m_device->m_queue->m_queue);
    m_textures.clear();
    if (m_swapchain != VK_NULL_HANDLE)
    {
        api.vkDestroySwapchainKHR(api.m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

Result SurfaceImpl::configure(const SurfaceConfig& config)
{
    setConfig(config);

    if (m_config.width == 0 || m_config.height == 0)
    {
        return SLANG_FAIL;
    }
    if (m_config.format == Format::Unknown)
    {
        m_config.format = m_info.preferredFormat;
    }

    destroySwapchain();
    SLANG_RETURN_ON_FAIL(createSwapchain());

    return SLANG_OK;
}

Result SurfaceImpl::getCurrentTexture(ITexture** outTexture)
{
    auto& api = m_device->m_api;

    if (m_textures.empty())
    {
        m_device->m_queue->m_pendingWaitSemaphores[1] = VK_NULL_HANDLE;
        return -1;
    }

    m_currentTextureIndex = -1;
    VkResult result = api.vkAcquireNextImageKHR(
        api.m_device,
        m_swapchain,
        UINT64_MAX,
        m_nextImageSemaphore,
        VK_NULL_HANDLE,
        (uint32_t*)&m_currentTextureIndex
    );

    if (result != VK_SUCCESS
#if SLANG_APPLE_FAMILY
        && result != VK_SUBOPTIMAL_KHR
#endif
    )
    {
        return SLANG_FAIL;
    }

    // Make the queue's next submit wait on `m_nextImageSemaphore`.
    m_device->m_queue->m_pendingWaitSemaphores[1] = m_nextImageSemaphore;
    returnComPtr(outTexture, m_textures[m_currentTextureIndex]);
    return SLANG_OK;
}

Result SurfaceImpl::present()
{
    auto& api = m_device->m_api;

    // If there are pending fence wait operations, flush them as an
    // empty vkQueueSubmit.
    if (!m_device->m_queue->m_pendingWaitFences.empty())
    {
        m_device->m_queue->queueSubmitImpl(0, nullptr, nullptr, 0);
    }

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &m_currentTextureIndex;
    static_vector<VkSemaphore, 2> waitSemaphores;
    for (auto s : m_device->m_queue->m_pendingWaitSemaphores)
    {
        if (s != VK_NULL_HANDLE)
        {
            waitSemaphores.push_back(s);
        }
    }
    m_device->m_queue->m_pendingWaitSemaphores[0] = VK_NULL_HANDLE;
    m_device->m_queue->m_pendingWaitSemaphores[1] = VK_NULL_HANDLE;
    presentInfo.waitSemaphoreCount = (uint32_t)waitSemaphores.size();
    if (presentInfo.waitSemaphoreCount)
    {
        presentInfo.pWaitSemaphores = waitSemaphores.data();
    }
    if (m_currentTextureIndex != -1)
    {
        api.vkQueuePresentKHR(m_device->m_queue->m_queue, &presentInfo);
        return SLANG_OK;
    }
    else
    {
        return SLANG_FAIL;
    }
}

Result DeviceImpl::createSurface(WindowHandle windowHandle, ISurface** outSurface)
{
    RefPtr<SurfaceImpl> surface = new SurfaceImpl();
    SLANG_RETURN_ON_FAIL(surface->init(this, windowHandle));
    returnComPtr(outSurface, surface);
    return SLANG_OK;
}

} // namespace rhi::vk
