#include "vk-surface.h"
#include "vk-device.h"
#include "vk-command.h"
#include "vk-texture.h"
#include "vk-utils.h"
#include "cocoa-util.h"

#include "core/static_vector.h"
#include "core/reverse-map.h"

#include <algorithm>
#include <vector>

namespace rhi::vk {

static auto translateVkFormat = reverseMap<Format, VkFormat>(getVkFormat, Format::Undefined, Format::_Count);

SurfaceImpl::~SurfaceImpl()
{
    auto& api = m_device->m_api;

    destroySwapchain();

    if (m_surface)
    {
        api.vkDestroySurfaceKHR(api.m_instance, m_surface, nullptr);
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

    switch (windowHandle.type)
    {
#if SLANG_WINDOWS_FAMILY
    case WindowHandleType::HWND:
    {
        VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
        surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surfaceCreateInfo.hinstance = ::GetModuleHandle(nullptr);
        surfaceCreateInfo.hwnd = (HWND)windowHandle.handleValues[0];
        SLANG_VK_RETURN_ON_FAIL(api.vkCreateWin32SurfaceKHR(api.m_instance, &surfaceCreateInfo, nullptr, &m_surface));
        break;
    }
#elif SLANG_APPLE_FAMILY
    case WindowHandleType::NSWindow:
    {
        m_metalLayer = CocoaUtil::createMetalLayer((void*)windowHandle.handleValues[0]);
        VkMetalSurfaceCreateInfoEXT surfaceCreateInfo = {};
        surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
        surfaceCreateInfo.pLayer = (CAMetalLayer*)m_metalLayer;
        SLANG_VK_RETURN_ON_FAIL(api.vkCreateMetalSurfaceEXT(api.m_instance, &surfaceCreateInfo, nullptr, &m_surface));
        break;
    }
#elif SLANG_LINUX_FAMILY
    case WindowHandleType::XlibWindow:
    {
        VkXlibSurfaceCreateInfoKHR surfaceCreateInfo = {};
        surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        surfaceCreateInfo.dpy = (Display*)windowHandle.handleValues[0];
        surfaceCreateInfo.window = (Window)windowHandle.handleValues[1];
        SLANG_VK_RETURN_ON_FAIL(api.vkCreateXlibSurfaceKHR(api.m_instance, &surfaceCreateInfo, nullptr, &m_surface));
        break;
    }
#endif
    default:
        return SLANG_E_INVALID_HANDLE;
    }

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

    Format preferredFormat = Format::Undefined;
    for (uint32_t i = 0; i < formatCount; ++i)
    {
        Format format = translateVkFormat(surfaceFormats[i].format);
        if (format != Format::Undefined)
            m_supportedFormats.push_back(format);
        if (preferredFormat == Format::Undefined &&
            (format == Format::RGBA8UnormSrgb || format == Format::BGRA8UnormSrgb || format == Format::BGRX8UnormSrgb))
            preferredFormat = format;
    }
    if (preferredFormat == Format::Undefined && !m_supportedFormats.empty())
    {
        preferredFormat = m_supportedFormats.front();
    }

    m_info.preferredFormat = preferredFormat;
    m_info.supportedUsage = TextureUsage::Present | TextureUsage::RenderTarget | TextureUsage::UnorderedAccess |
                            TextureUsage::CopyDestination;
    m_info.formats = m_supportedFormats.data();
    m_info.formatCount = (uint32_t)m_supportedFormats.size();

    return SLANG_OK;
}

Result SurfaceImpl::createSwapchain()
{
    auto& api = m_device->m_api;

    VkExtent2D imageExtent = {m_config.width, m_config.height};

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

    VkFormat format = getVkFormat(m_config.format);
    VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE;

    VkSwapchainCreateInfoKHR swapchainDesc = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchainDesc.surface = m_surface;
    swapchainDesc.minImageCount = m_config.desiredImageCount;
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
        textureDesc.type = TextureType::Texture2D;
        textureDesc.size.width = m_config.width;
        textureDesc.size.height = m_config.height;
        textureDesc.size.depth = 1;
        textureDesc.arrayLength = 1;
        textureDesc.mipCount = 1;
        textureDesc.format = m_config.format;
        textureDesc.usage = m_config.usage;
        textureDesc.defaultState = ResourceState::Present;
        RefPtr<TextureImpl> texture = new TextureImpl(m_device, textureDesc);
        texture->m_image = swapchainImages[i];
        texture->m_imageMemory = 0;
        texture->m_vkformat = format;
        texture->m_isWeakImageReference = true;
        m_textures.push_back(texture);
    }

    m_frameData.resize(swapchainImageCount);
    for (uint32_t i = 0; i < swapchainImageCount; ++i)
    {
        FrameData& frameData = m_frameData[i];
        // Create fence.
        {
            VkFenceCreateInfo createInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            SLANG_VK_RETURN_ON_FAIL(api.vkCreateFence(api.m_device, &createInfo, nullptr, &frameData.fence));
        }

        // Create semaphores.
        {
            VkSemaphoreCreateInfo createInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            SLANG_VK_RETURN_ON_FAIL(
                api.vkCreateSemaphore(api.m_device, &createInfo, nullptr, &frameData.imageAvailableSemaphore)
            );
            SLANG_VK_RETURN_ON_FAIL(
                api.vkCreateSemaphore(api.m_device, &createInfo, nullptr, &frameData.renderFinishedSemaphore)
            );
        }
    }

    m_currentFrameIndex = 0;

    return SLANG_OK;
}

void SurfaceImpl::destroySwapchain()
{
    auto& api = m_device->m_api;
    api.vkQueueWaitIdle(m_device->m_queue->m_queue);
    m_textures.clear();
    for (FrameData& frameData : m_frameData)
    {
        if (frameData.fence != VK_NULL_HANDLE)
        {
            api.vkDestroyFence(api.m_device, frameData.fence, nullptr);
        }
        if (frameData.imageAvailableSemaphore != VK_NULL_HANDLE)
        {
            api.vkDestroySemaphore(api.m_device, frameData.imageAvailableSemaphore, nullptr);
        }
        if (frameData.renderFinishedSemaphore != VK_NULL_HANDLE)
        {
            api.vkDestroySemaphore(api.m_device, frameData.renderFinishedSemaphore, nullptr);
        }
    }
    m_frameData.clear();
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
    if (m_config.format == Format::Undefined)
    {
        m_config.format = m_info.preferredFormat;
    }
    FormatSupport formatSupport = {};
    m_device->getFormatSupport(m_config.format, &formatSupport);
    if (m_config.usage == TextureUsage::None)
    {
        m_config.usage = TextureUsage::Present | TextureUsage::RenderTarget | TextureUsage::CopyDestination;
        if (is_set(formatSupport, FormatSupport::ShaderUavStore))
        {
            m_config.usage |= TextureUsage::UnorderedAccess;
        }
    }
    else
    {
        if (!is_set(formatSupport, FormatSupport::RenderTarget) && is_set(m_config.usage, TextureUsage::RenderTarget))
        {
            m_device->printError("Surface format does not support render target usage.");
            return SLANG_E_INVALID_ARG;
        }
        if (!is_set(formatSupport, FormatSupport::CopyDestination) &&
            is_set(m_config.usage, TextureUsage::CopyDestination))
        {
            m_device->printError("Surface format does not support copy destination usage.");
            return SLANG_E_INVALID_ARG;
        }
        if (!is_set(formatSupport, FormatSupport::ShaderUavStore) &&
            is_set(m_config.usage, TextureUsage::UnorderedAccess))
        {
            m_device->printError("Surface format does not support unordered access usage.");
            return SLANG_E_INVALID_ARG;
        }
    }

    m_configured = false;
    destroySwapchain();
    SLANG_RETURN_ON_FAIL(createSwapchain());
    m_configured = true;

    return SLANG_OK;
}

Result SurfaceImpl::unconfigure()
{
    if (!m_configured)
    {
        return SLANG_OK;
    }

    m_configured = false;
    destroySwapchain();

    return SLANG_OK;
}

Result SurfaceImpl::acquireNextImage(ITexture** outTexture)
{
    *outTexture = nullptr;

    if (!m_configured)
    {
        return SLANG_FAIL;
    }

    auto& api = m_device->m_api;

    FrameData& frameData = m_frameData[m_currentFrameIndex];
    SLANG_VK_RETURN_ON_FAIL(api.vkWaitForFences(api.m_device, 1, &frameData.fence, VK_TRUE, UINT64_MAX));
    SLANG_VK_RETURN_ON_FAIL(api.vkResetFences(api.m_device, 1, &frameData.fence));

    m_currentTextureIndex = -1;
    VkResult result = api.vkAcquireNextImageKHR(
        api.m_device,
        m_swapchain,
        UINT64_MAX,
        frameData.imageAvailableSemaphore,
        VK_NULL_HANDLE,
        (uint32_t*)&m_currentTextureIndex
    );

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        return SLANG_FAIL;
    }

    // Setup queue's next submit for synchronization with the swapchain.
    m_device->m_queue->m_surfaceSync.fence = frameData.fence;
    m_device->m_queue->m_surfaceSync.imageAvailableSemaphore = frameData.imageAvailableSemaphore;
    m_device->m_queue->m_surfaceSync.renderFinishedSemaphore = frameData.renderFinishedSemaphore;

    // Mark texture to be in swapchain initial state.
    // This is used by the first image barrier to transition the texture from the correct state.
    m_textures[m_currentTextureIndex]->m_isSwapchainInitialState = true;

    returnComPtr(outTexture, m_textures[m_currentTextureIndex]);
    return SLANG_OK;
}

Result SurfaceImpl::present()
{
    auto& api = m_device->m_api;

    if (!m_configured)
    {
        return SLANG_FAIL;
    }

    FrameData& frameData = m_frameData[m_currentFrameIndex];
    m_currentFrameIndex = (m_currentFrameIndex + 1) % m_frameData.size();

    // If no submit has taken place yet, then we need to submit a dummy command buffer to transition the texture to the
    // correct state.
    if (m_device->m_queue->m_surfaceSync.fence != VK_NULL_HANDLE)
    {
        ICommandQueue* queue = m_device->m_queue.get();
        ComPtr<ICommandEncoder> encoder;
        SLANG_RETURN_ON_FAIL(queue->createCommandEncoder(encoder.writeRef()));
        encoder->setTextureState(m_textures[m_currentTextureIndex], ResourceState::General);
        SLANG_RETURN_ON_FAIL(queue->submit(encoder->finish()));
    }

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &m_currentTextureIndex;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frameData.renderFinishedSemaphore;
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
