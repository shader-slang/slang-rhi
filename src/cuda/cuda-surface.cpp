#include "cuda-surface.h"

#if SLANG_RHI_ENABLE_VULKAN
#include "cuda-device.h"
#include "cuda-texture.h"
#include "cuda-command.h"
#include "cuda-utils.h"

#include "vulkan/vk-api.h"
#include "vulkan/vk-utils.h"

#include "core/reverse-map.h"
#include "core/short_vector.h"

#if SLANG_WINDOWS_FAMILY
#include <dxgi1_4.h>
#endif

#if !SLANG_WINDOWS_FAMILY
#include <unistd.h>
#endif

#include <algorithm>

// Note:
// CUDA doesn't provide a swapchain implementation.
// In order to support the ISurface interface in the CUDA backend,
// this implementation is based on a Vulkan swapchain.
// On the Vulkan side, we create a normal Vulkan based swapchain.
// To allow passing textures to CUDA, a set of "virtual" swapchain images are created.
// These images are allocated in Vulkan and shared with CUDA.
// Calls to `ISurface::acquireNextImage` return these shared textures.
// Calls to `ISurface::present` copy the contents of the shared texture to the Vulkan swapchain image.

namespace rhi::cuda {

/// Enable the Vulkan validation layer.
static constexpr bool kEnableValidation = true;

struct SharedTexture
{
    VkImage vulkanImage;
    VkDeviceMemory vulkanMemory;
    NativeHandle sharedHandle;
    RefPtr<TextureImpl> cudaTexture;
};

struct FrameData
{
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    // Fence to signal when the rendering to the swapchain image is finished.
    VkFence fence;
    // Semaphore to signal when the swapchain image is available.
    VkSemaphore imageAvailableSemaphore;
    // Semaphore to signal when the rendering to the swapchain image is finished.
    VkSemaphore renderFinishedSemaphore;
    // Semaphore to signal when the shared texture is ready.
    VkSemaphore sharedSemaphore;
    NativeHandle sharedSemaphoreHandle;
    CUexternalSemaphore cudaSemaphore;
    uint64_t signalValue;

    SharedTexture sharedTexture;
};

class SurfaceImpl : public Surface
{
public:
    RefPtr<DeviceImpl> m_deviceImpl;
    WindowHandle m_windowHandle;
    std::vector<Format> m_supportedFormats;

    vk::VulkanModule m_module;
    vk::VulkanApi m_api;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    uint32_t m_queueFamilyIndex = 0;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;

    short_vector<FrameData> m_frameData;
    uint32_t m_currentFrameIndex = 0;
    short_vector<VkImage> m_swapchainImages;
    uint32_t m_currentSwapchainImageIndex = -1;

public:
    ~SurfaceImpl();

    Result init(DeviceImpl* device, WindowHandle windowHandle);

    Result createVulkanInstance();
    Result createVulkanDevice();

    Result createSwapchain();
    void destroySwapchain();

    Result createFrameData(FrameData& frameData);
    void destroyFrameData(FrameData& frameData);

    Result createSharedTexture(SharedTexture& sharedTexture);
    void destroySharedTexture(SharedTexture& sharedTexture);

    virtual SLANG_NO_THROW Result SLANG_MCALL configure(const SurfaceConfig& config) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unconfigure() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL acquireNextImage(ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL present() override;
};

static auto translateVkFormat = reverseMap<Format, VkFormat>(vk::getVkFormat, Format::Undefined, Format::_Count);

SurfaceImpl::~SurfaceImpl()
{
    SLANG_CUDA_CTX_SCOPE(m_deviceImpl);

    destroySwapchain();

    if (m_surface)
    {
        m_api.vkDestroySurfaceKHR(m_api.m_instance, m_surface, nullptr);
    }
    if (m_device)
    {
        m_api.vkDestroyDevice(m_device, nullptr);
    }
    if (m_instance)
    {
        m_api.vkDestroyInstance(m_instance, nullptr);
    }
    if (m_module.isInitialized())
    {
        m_module.destroy();
    }
}

Result SurfaceImpl::init(DeviceImpl* device, WindowHandle windowHandle)
{
    m_deviceImpl = device;
    m_windowHandle = windowHandle;

    SLANG_CUDA_CTX_SCOPE(m_deviceImpl);

    SLANG_RETURN_ON_FAIL(createVulkanInstance());

    switch (windowHandle.type)
    {
#if SLANG_WINDOWS_FAMILY
    case WindowHandleType::HWND:
    {
        VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
        surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surfaceCreateInfo.hinstance = ::GetModuleHandle(nullptr);
        surfaceCreateInfo.hwnd = (HWND)windowHandle.handleValues[0];
        SLANG_VK_RETURN_ON_FAIL(
            m_api.vkCreateWin32SurfaceKHR(m_api.m_instance, &surfaceCreateInfo, nullptr, &m_surface)
        );
        break;
    }
#elif SLANG_LINUX_FAMILY
    case WindowHandleType::XlibWindow:
    {
        VkXlibSurfaceCreateInfoKHR surfaceCreateInfo = {};
        surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        surfaceCreateInfo.dpy = (Display*)windowHandle.handleValues[0];
        surfaceCreateInfo.window = (Window)windowHandle.handleValues[1];
        SLANG_VK_RETURN_ON_FAIL(
            m_api.vkCreateXlibSurfaceKHR(m_api.m_instance, &surfaceCreateInfo, nullptr, &m_surface)
        );
        break;
    }
#endif
    default:
        return SLANG_E_INVALID_HANDLE;
    }

    SLANG_RETURN_ON_FAIL(createVulkanDevice());

    uint32_t formatCount = 0;
    m_api.vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    m_api.vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, surfaceFormats.data());

    Format preferredFormat = Format::Undefined;
    for (uint32_t i = 0; i < formatCount; ++i)
    {
        Format format = translateVkFormat(surfaceFormats[i].format);
        // Skip BGR formats that are not supported by the CUDA backend.
        if (format == Format::BGRA8Unorm || format == Format::BGRA8UnormSrgb || format == Format::BGRX8Unorm ||
            format == Format::BGRX8UnormSrgb)
            continue;
        if (format != Format::Undefined)
            m_supportedFormats.push_back(format);
        if (format == Format::RGBA8UnormSrgb)
            preferredFormat = format;
    }
    if (preferredFormat == Format::Undefined && !m_supportedFormats.empty())
    {
        preferredFormat = m_supportedFormats.front();
    }

    m_info.preferredFormat = preferredFormat;
    m_info.supportedUsage = TextureUsage::Present | TextureUsage::UnorderedAccess | TextureUsage::CopyDestination;
    m_info.formats = m_supportedFormats.data();
    m_info.formatCount = (uint32_t)m_supportedFormats.size();

    return SLANG_OK;
}

Result SurfaceImpl::createVulkanInstance()
{
    SLANG_CUDA_CTX_SCOPE(m_deviceImpl);

    SLANG_RETURN_ON_FAIL(m_module.init());
    SLANG_RETURN_ON_FAIL(m_api.initGlobalProcs(m_module));

    VkApplicationInfo applicationInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    applicationInfo.apiVersion = VK_API_VERSION_1_2;

    short_vector<const char*, 16> instanceExtensions;

    instanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    instanceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);

    instanceExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    // Note: this extension is not yet supported by nvidia drivers, disable for now.
    // instanceExtensions.push_back("VK_GOOGLE_surfaceless_query");
#if SLANG_WINDOWS_FAMILY
    instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif SLANG_LINUX_FAMILY
    instanceExtensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo instanceCreateInfo = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceCreateInfo.pApplicationInfo = &applicationInfo;
    instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
    instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();

    const char* layerNames[] = {nullptr};

    if (kEnableValidation)
    {
        uint32_t layerCount;
        m_api.vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers;
        availableLayers.resize(layerCount);
        m_api.vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (auto& layer : availableLayers)
        {
            if (strncmp(layer.layerName, "VK_LAYER_KHRONOS_validation", sizeof("VK_LAYER_KHRONOS_validation")) == 0)
            {
                layerNames[0] = "VK_LAYER_KHRONOS_validation";
                break;
            }
        }
        if (layerNames[0])
        {
            instanceCreateInfo.enabledLayerCount = SLANG_COUNT_OF(layerNames);
            instanceCreateInfo.ppEnabledLayerNames = layerNames;
        }
    }

    SLANG_VK_RETURN_ON_FAIL(m_api.vkCreateInstance(&instanceCreateInfo, nullptr, &m_instance));

    SLANG_RETURN_ON_FAIL(m_api.initInstanceProcs(m_instance));

    return SLANG_OK;
}

Result SurfaceImpl::createVulkanDevice()
{
    SLANG_CUDA_CTX_SCOPE(m_deviceImpl);

    uint32_t physicalDeviceCount = 0;
    SLANG_VK_RETURN_ON_FAIL(m_api.vkEnumeratePhysicalDevices(m_instance, &physicalDeviceCount, nullptr));

    std::vector<VkPhysicalDevice> physicalDevices;
    physicalDevices.resize(physicalDeviceCount);
    SLANG_VK_RETURN_ON_FAIL(m_api.vkEnumeratePhysicalDevices(m_instance, &physicalDeviceCount, physicalDevices.data()));

    // On Windows we match device by LUID. On Linux with UUID.
#if SLANG_WINDOWS_FAMILY
    char cudaLUID[8] = {};
    unsigned int deviceNodeMask;
    SLANG_CUDA_ASSERT_ON_FAIL(cuDeviceGetLuid(cudaLUID, &deviceNodeMask, m_deviceImpl->m_ctx.device));
#elif SLANG_LINUX_FAMILY
    CUuuid cudaUUID = {};
    SLANG_CUDA_ASSERT_ON_FAIL(cuDeviceGetUuid(&cudaUUID, m_deviceImpl->m_ctx.device));
#else
#error "Unsupported platform"
#endif

    if (!m_api.vkGetPhysicalDeviceFeatures2)
    {
        return SLANG_FAIL;
    }

    for (uint32_t i = 0; i < physicalDeviceCount; ++i)
    {
        // Get Vulkan device LUID.
        VkPhysicalDeviceIDPropertiesKHR idProps = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES_KHR};
        VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        props.pNext = &idProps;
        m_api.vkGetPhysicalDeviceProperties2(physicalDevices[i], &props);

        // Check if the device LUID/UUID matches the CUDA device.
#if SLANG_WINDOWS_FAMILY
        if (!idProps.deviceLUIDValid || ::memcmp(cudaLUID, idProps.deviceLUID, 8) != 0)
        {
            continue;
        }
#elif SLANG_LINUX_FAMILY
        if (::memcmp(&cudaUUID, idProps.deviceUUID, 16) != 0)
        {
            continue;
        }
#endif

        uint32_t queueFamilyCount = 0;
        m_api.vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[i], &queueFamilyCount, NULL);
        std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
        m_api.vkGetPhysicalDeviceQueueFamilyProperties(
            physicalDevices[i],
            &queueFamilyCount,
            queueFamilyProperties.data()
        );

        for (uint32_t j = 0; j < queueFamilyCount; ++j)
        {

            VkBool32 supportsPresent = VK_FALSE;
            m_api.vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevices[i], j, m_surface, &supportsPresent);

            if (supportsPresent && (queueFamilyProperties[j].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                m_queueFamilyIndex = j;
                m_physicalDevice = physicalDevices[i];
                break;
            }
        }

        if (m_physicalDevice)
        {
            break;
        }
    }

    if (!m_physicalDevice)
    {
        return SLANG_FAIL;
    }

    SLANG_RETURN_ON_FAIL(m_api.initPhysicalDevice(m_physicalDevice));

    VkPhysicalDeviceFeatures2 deviceFeatures2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    deviceFeatures2.pNext = &m_api.m_extendedFeatures.vulkan12Features;
    m_api.vkGetPhysicalDeviceFeatures2(m_physicalDevice, &deviceFeatures2);

    std::vector<const char*> deviceExtensions;
    deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
#if SLANG_WINDOWS_FAMILY
    deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
#else
    deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
#endif

    float queuePriority = 1.f;
    VkDeviceQueueCreateInfo queueCreateInfo = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueCreateInfo.queueFamilyIndex = m_queueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.enabledLayerCount = 0;
    deviceCreateInfo.ppEnabledLayerNames = nullptr;
    deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
    deviceCreateInfo.pNext = &deviceFeatures2;

    SLANG_VK_RETURN_ON_FAIL(m_api.vkCreateDevice(m_physicalDevice, &deviceCreateInfo, nullptr, &m_device));

    SLANG_RETURN_ON_FAIL(m_api.initDeviceProcs(m_device));

    m_api.vkGetDeviceQueue(m_device, m_queueFamilyIndex, 0, &m_queue);

    return SLANG_OK;
}

Result SurfaceImpl::createSwapchain()
{
    SLANG_CUDA_CTX_SCOPE(m_deviceImpl);

    VkExtent2D imageExtent = {m_config.width, m_config.height};

    // It is necessary to query the caps -> otherwise the LunarG verification layer will
    // issue an error
    {
        VkSurfaceCapabilitiesKHR surfaceCaps;

        SLANG_VK_RETURN_ON_FAIL(
            m_api.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &surfaceCaps)
        );
    }

    // Query available present modes.
    uint32_t presentModeCount = 0;
    m_api.vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    m_api
        .vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, presentModes.data());

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

    VkFormat format = vk::getVkFormat(m_config.format);
    VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE;

    VkSwapchainCreateInfoKHR swapchainDesc = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchainDesc.surface = m_surface;
    swapchainDesc.minImageCount = m_config.desiredImageCount;
    swapchainDesc.imageFormat = format;
    swapchainDesc.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainDesc.imageExtent = imageExtent;
    swapchainDesc.imageArrayLayers = 1;
    swapchainDesc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchainDesc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainDesc.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchainDesc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainDesc.presentMode = selectedPresentMode;
    swapchainDesc.clipped = VK_TRUE;
    swapchainDesc.oldSwapchain = oldSwapchain;

    SLANG_VK_RETURN_ON_FAIL(m_api.vkCreateSwapchainKHR(m_device, &swapchainDesc, nullptr, &m_swapchain));

    uint32_t swapchainImageCount = 0;
    m_api.vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, nullptr);
    m_swapchainImages.resize(swapchainImageCount);
    m_api.vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, m_swapchainImages.data());

    // Create frame data.
    m_frameData.resize(swapchainImageCount);
    for (uint32_t i = 0; i < swapchainImageCount; ++i)
    {
        SLANG_RETURN_ON_FAIL(createFrameData(m_frameData[i]));
    }
    m_currentFrameIndex = 0;

    return SLANG_OK;
}

void SurfaceImpl::destroySwapchain()
{
    SLANG_CUDA_CTX_SCOPE(m_deviceImpl);

    if (m_queue != VK_NULL_HANDLE)
    {
        m_api.vkQueueWaitIdle(m_queue);
    }

    for (auto& frameData : m_frameData)
    {
        destroyFrameData(frameData);
    }
    m_frameData.clear();

    if (m_swapchain != VK_NULL_HANDLE)
    {
        m_api.vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

Result SurfaceImpl::createFrameData(FrameData& frameData)
{
    SLANG_CUDA_CTX_SCOPE(m_deviceImpl);

    frameData = {0};

    // Create command pool.
    {
        VkCommandPoolCreateInfo createInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        createInfo.queueFamilyIndex = m_queueFamilyIndex;
        SLANG_VK_RETURN_ON_FAIL(m_api.vkCreateCommandPool(m_device, &createInfo, 0, &frameData.commandPool));
    }

    // Allocate command buffer.
    {
        VkCommandBufferAllocateInfo allocateInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocateInfo.commandPool = frameData.commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        SLANG_VK_RETURN_ON_FAIL(m_api.vkAllocateCommandBuffers(m_device, &allocateInfo, &frameData.commandBuffer));
    }

    // Create fence.
    {
        VkFenceCreateInfo createInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        SLANG_VK_RETURN_ON_FAIL(m_api.vkCreateFence(m_device, &createInfo, nullptr, &frameData.fence));
    }

    // Create semaphores.
    {
        VkSemaphoreCreateInfo createInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        SLANG_VK_RETURN_ON_FAIL(
            m_api.vkCreateSemaphore(m_device, &createInfo, nullptr, &frameData.imageAvailableSemaphore)
        );
        SLANG_VK_RETURN_ON_FAIL(
            m_api.vkCreateSemaphore(m_device, &createInfo, nullptr, &frameData.renderFinishedSemaphore)
        );
    }

    // Create semaphore shared with CUDA.
    {
        VkExportSemaphoreCreateInfo exportInfo = {VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
#if SLANG_WINDOWS_FAMILY
        exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
        exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif

        VkSemaphoreTypeCreateInfo typeCreateInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        typeCreateInfo.pNext = &exportInfo;
        typeCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        typeCreateInfo.initialValue = 0;

        VkSemaphoreCreateInfo createInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        createInfo.pNext = &typeCreateInfo;
        SLANG_VK_RETURN_ON_FAIL(m_api.vkCreateSemaphore(m_device, &createInfo, nullptr, &frameData.sharedSemaphore));

#if SLANG_WINDOWS_FAMILY
        VkSemaphoreGetWin32HandleInfoKHR handleInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR};
        handleInfo.semaphore = frameData.sharedSemaphore;
        handleInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        frameData.sharedSemaphoreHandle.type = NativeHandleType::Win32;
        SLANG_VK_RETURN_ON_FAIL(
            m_api.vkGetSemaphoreWin32HandleKHR(m_device, &handleInfo, (HANDLE*)(&frameData.sharedSemaphoreHandle.value))
        );
#else
        VkSemaphoreGetFdInfoKHR fdInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
        fdInfo.semaphore = frameData.sharedSemaphore;
        fdInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
        frameData.sharedSemaphoreHandle.type = NativeHandleType::FileDescriptor;
        SLANG_VK_RETURN_ON_FAIL(
            m_api.vkGetSemaphoreFdKHR(m_device, &fdInfo, (int*)(&frameData.sharedSemaphoreHandle.value))
        );
#endif

        CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC externalSemaphoreHandleDesc = {};
#if SLANG_WINDOWS_FAMILY
        externalSemaphoreHandleDesc.type = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_WIN32;
        externalSemaphoreHandleDesc.handle.win32.handle = (void*)frameData.sharedSemaphoreHandle.value;
#else
        externalSemaphoreHandleDesc.type = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_FD;
        externalSemaphoreHandleDesc.handle.fd = (int)frameData.sharedSemaphoreHandle.value;
#endif
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(
            cuImportExternalSemaphore(&frameData.cudaSemaphore, &externalSemaphoreHandleDesc),
            m_deviceImpl.get()
        );
    }

    SLANG_RETURN_ON_FAIL(createSharedTexture(frameData.sharedTexture));

    return SLANG_OK;
}

void SurfaceImpl::destroyFrameData(FrameData& frameData)
{
    SLANG_CUDA_CTX_SCOPE(m_deviceImpl);

    if (frameData.commandBuffer)
    {
        m_api.vkFreeCommandBuffers(m_device, frameData.commandPool, 1, &frameData.commandBuffer);
    }
    if (frameData.commandPool)
    {
        m_api.vkDestroyCommandPool(m_device, frameData.commandPool, nullptr);
    }
    if (frameData.fence)
    {
        m_api.vkDestroyFence(m_device, frameData.fence, nullptr);
    }
    if (frameData.imageAvailableSemaphore)
    {
        m_api.vkDestroySemaphore(m_device, frameData.imageAvailableSemaphore, nullptr);
    }
    if (frameData.renderFinishedSemaphore)
    {
        m_api.vkDestroySemaphore(m_device, frameData.renderFinishedSemaphore, nullptr);
    }
    if (frameData.sharedSemaphore)
    {
        if (frameData.sharedSemaphoreHandle.value)
        {
#if SLANG_WINDOWS_FAMILY
            ::CloseHandle((HANDLE)frameData.sharedSemaphoreHandle.value);
#else
            ::close((int)frameData.sharedSemaphoreHandle.value);
#endif
        }

        m_api.vkDestroySemaphore(m_device, frameData.sharedSemaphore, nullptr);
    }
    destroySharedTexture(frameData.sharedTexture);
}

Result SurfaceImpl::createSharedTexture(SharedTexture& sharedTexture)
{
    SLANG_CUDA_CTX_SCOPE(m_deviceImpl);

    VkImageCreateInfo imageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = VkExtent3D{m_config.width, m_config.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = vk::getVkFormat(m_config.format);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkExternalMemoryImageCreateInfo externalMemoryImageCreateInfo = {
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO
    };
    VkExternalMemoryHandleTypeFlags extMemoryHandleType =
#if SLANG_WINDOWS_FAMILY
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
    externalMemoryImageCreateInfo.pNext = nullptr;
    externalMemoryImageCreateInfo.handleTypes = extMemoryHandleType;
    imageInfo.pNext = &externalMemoryImageCreateInfo;

    SLANG_VK_RETURN_ON_FAIL(m_api.vkCreateImage(m_device, &imageInfo, nullptr, &sharedTexture.vulkanImage));

    VkMemoryRequirements memRequirements;
    m_api.vkGetImageMemoryRequirements(m_device, sharedTexture.vulkanImage, &memRequirements);

    // Allocate the memory
    int memoryTypeIndex =
        m_api.findMemoryTypeIndex(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    SLANG_RHI_ASSERT(memoryTypeIndex >= 0);

    VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
#if SLANG_WINDOWS_FAMILY
    VkExportMemoryWin32HandleInfoKHR exportMemoryWin32HandleInfo = {
        VK_STRUCTURE_TYPE_EXPORT_MEMORY_WIN32_HANDLE_INFO_KHR
    };
#endif
    VkExportMemoryAllocateInfoKHR exportMemoryAllocateInfo = {VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR};
#if SLANG_WINDOWS_FAMILY
    exportMemoryWin32HandleInfo.pNext = nullptr;
    exportMemoryWin32HandleInfo.pAttributes = nullptr;
    exportMemoryWin32HandleInfo.dwAccess = DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE;
    exportMemoryWin32HandleInfo.name = NULL;

    exportMemoryAllocateInfo.pNext = extMemoryHandleType & VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR
                                         ? &exportMemoryWin32HandleInfo
                                         : nullptr;
#endif
    exportMemoryAllocateInfo.handleTypes = extMemoryHandleType;
    allocInfo.pNext = &exportMemoryAllocateInfo;

    SLANG_VK_RETURN_ON_FAIL(m_api.vkAllocateMemory(m_device, &allocInfo, nullptr, &sharedTexture.vulkanMemory));

    // Bind the memory to the image
    m_api.vkBindImageMemory(m_device, sharedTexture.vulkanImage, sharedTexture.vulkanMemory, 0);

    // Create shared handle
#if SLANG_WINDOWS_FAMILY
    VkMemoryGetWin32HandleInfoKHR getHandleInfo = {VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR};
    getHandleInfo.memory = sharedTexture.vulkanMemory;
    getHandleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    if (!m_api.vkGetMemoryWin32HandleKHR)
    {
        return SLANG_FAIL;
    }
    sharedTexture.sharedHandle.type = NativeHandleType::Win32;
    SLANG_VK_RETURN_ON_FAIL(
        m_api.vkGetMemoryWin32HandleKHR(m_device, &getHandleInfo, (HANDLE*)&sharedTexture.sharedHandle.value)
    );
#else
    VkMemoryGetFdInfoKHR getHandleInfo = {VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
    getHandleInfo.memory = sharedTexture.vulkanMemory;
    ;
    getHandleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    if (!m_api.vkGetMemoryFdKHR)
    {
        return SLANG_FAIL;
    }
    sharedTexture.sharedHandle.type = NativeHandleType::FileDescriptor;
    SLANG_VK_RETURN_ON_FAIL(m_api.vkGetMemoryFdKHR(m_device, &getHandleInfo, (int*)&sharedTexture.sharedHandle.value));
#endif

    // Create CUDA texture.
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
    SLANG_RETURN_ON_FAIL(m_deviceImpl->createTextureFromSharedHandle(
        sharedTexture.sharedHandle,
        textureDesc,
        memRequirements.size,
        (ITexture**)sharedTexture.cudaTexture.writeRef()
    ));

    return SLANG_OK;
}

void SurfaceImpl::destroySharedTexture(SharedTexture& sharedTexture)
{
    SLANG_CUDA_CTX_SCOPE(m_deviceImpl);

    sharedTexture.cudaTexture = nullptr;
    if (sharedTexture.sharedHandle)
    {
#if SLANG_WINDOWS_FAMILY
        ::CloseHandle((HANDLE)sharedTexture.sharedHandle.value);
#else
        ::close((int)sharedTexture.sharedHandle.value);
#endif
    }
    if (sharedTexture.vulkanImage)
    {
        m_api.vkDestroyImage(m_device, sharedTexture.vulkanImage, nullptr);
    }
    if (sharedTexture.vulkanMemory)
    {
        m_api.vkFreeMemory(m_device, sharedTexture.vulkanMemory, nullptr);
    }
}

Result SurfaceImpl::configure(const SurfaceConfig& config)
{
    SLANG_CUDA_CTX_SCOPE(m_deviceImpl);

    setConfig(config);

    if (m_config.width == 0 || m_config.height == 0)
    {
        return SLANG_FAIL;
    }
    if (m_config.format == Format::Undefined)
    {
        m_config.format = m_info.preferredFormat;
    }
    if (m_config.usage == TextureUsage::None)
    {
        m_config.usage = m_info.supportedUsage;
    }

    m_configured = false;
    destroySwapchain();
    SLANG_RETURN_ON_FAIL(createSwapchain());
    m_configured = true;

    return SLANG_OK;
}

Result SurfaceImpl::unconfigure()
{
    SLANG_CUDA_CTX_SCOPE(m_deviceImpl);

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
    SLANG_CUDA_CTX_SCOPE(m_deviceImpl);

    *outTexture = nullptr;

    if (!m_configured)
    {
        return SLANG_FAIL;
    }

    FrameData& frameData = m_frameData[m_currentFrameIndex];

    SLANG_VK_RETURN_ON_FAIL(m_api.vkWaitForFences(m_device, 1, &frameData.fence, VK_TRUE, UINT64_MAX));
    SLANG_VK_RETURN_ON_FAIL(m_api.vkResetFences(m_device, 1, &frameData.fence));

    SLANG_VK_RETURN_ON_FAIL(m_api.vkResetCommandBuffer(frameData.commandBuffer, 0));

    m_currentSwapchainImageIndex = -1;
    VkResult result = m_api.vkAcquireNextImageKHR(
        m_device,
        m_swapchain,
        UINT64_MAX,
        frameData.imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &m_currentSwapchainImageIndex
    );

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        return SLANG_FAIL;
    }

    returnComPtr(outTexture, frameData.sharedTexture.cudaTexture);
    return SLANG_OK;
}

Result SurfaceImpl::present()
{
    SLANG_CUDA_CTX_SCOPE(m_deviceImpl);

    if (!m_configured)
    {
        return SLANG_FAIL;
    }
    if (m_currentSwapchainImageIndex == -1)
    {
        return SLANG_FAIL;
    }

    FrameData& frameData = m_frameData[m_currentFrameIndex];
    m_currentFrameIndex = (m_currentFrameIndex + 1) % m_frameData.size();
    VkImage swapchainImage = m_swapchainImages[m_currentSwapchainImageIndex];
    VkImage sharedImage = frameData.sharedTexture.vulkanImage;

    // On classic graphics devices surface presentation would syncronize with the graphics queue. This
    // is emulated in CUDA by treating the default (NULL) CUDA stream as the graphics queue.

    // Signal semaphore on CUDA stream.
    // This would be the preferred way, but leads to Vulkan validation errors because
    // the validation layer cannot se the signal sent from the CUDA stream.
#if 0
    {
        CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS signalParams = {};
        signalParams.params.fence.value = ++frameData.signalValue;
        cuSignalExternalSemaphoresAsync(&frameData.cudaSemaphore, &signalParams, 1, m_deviceImpl->m_queue->m_stream);
    }
#endif

    // As the semaphore approach doesn't currently work, call cuStreamSynchronize, which blocks
    // the host until the default CUDA stream is completely drained.
#if 1
    {
        cuStreamSynchronize(m_deviceImpl->m_queue->m_stream);
        VkSemaphoreSignalInfo signalInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
        signalInfo.semaphore = frameData.sharedSemaphore;
        signalInfo.value = ++frameData.signalValue;
        SLANG_VK_RETURN_ON_FAIL(m_api.vkSignalSemaphore(m_device, &signalInfo));
    }
#endif

    VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    SLANG_VK_RETURN_ON_FAIL(m_api.vkBeginCommandBuffer(frameData.commandBuffer, &beginInfo));

    VkImageSubresourceRange subresourceRange;
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = 1;

    // Change layout of swapchain image to be optimal for transfer destination
    {
        VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = m_queueFamilyIndex;
        barrier.dstQueueFamilyIndex = m_queueFamilyIndex;
        barrier.image = swapchainImage;
        barrier.subresourceRange = subresourceRange;
        m_api.vkCmdPipelineBarrier(
            frameData.commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );
    }

    // Change layout of shared texture to be optimal for transfer source
    {
        VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
        barrier.dstQueueFamilyIndex = m_queueFamilyIndex;
        barrier.image = sharedImage;
        barrier.subresourceRange = subresourceRange;
        m_api.vkCmdPipelineBarrier(
            frameData.commandBuffer,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );
    }

    // Clear swapchain image (testing)
#if 0
    {
        VkClearColorValue clearColor = {1.f, 0.f, 1.f, 1.f};
        m_api.vkCmdClearColorImage(
            frameData.commandBuffer,
            swapchainImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clearColor,
            1,
            &subresourceRange
        );
    }
#endif

    // Copy shared image to swapchain image
    {
        VkImageCopy imageCopy = {};
        imageCopy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopy.srcSubresource.mipLevel = 0;
        imageCopy.srcSubresource.baseArrayLayer = 0;
        imageCopy.srcSubresource.layerCount = 1;
        imageCopy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopy.dstSubresource.mipLevel = 0;
        imageCopy.dstSubresource.baseArrayLayer = 0;
        imageCopy.dstSubresource.layerCount = 1;
        imageCopy.extent = {m_config.width, m_config.height, 1};
        m_api.vkCmdCopyImage(
            frameData.commandBuffer,
            sharedImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            swapchainImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &imageCopy
        );
    }

#if 0
    // Change layout of shared texture to be optimal for CUDA
    {
        VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = m_queueFamilyIndex;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
        barrier.image = sharedImage;
        barrier.subresourceRange = subresourceRange;
        m_api.vkCmdPipelineBarrier(
            frameData.commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );
    }
#endif

    // Change layout of swapchain image to be optimal for presenting
    {
        VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex = m_queueFamilyIndex;
        barrier.dstQueueFamilyIndex = m_queueFamilyIndex;
        barrier.image = swapchainImage;
        barrier.subresourceRange = subresourceRange;
        m_api.vkCmdPipelineBarrier(
            frameData.commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );
    }

    SLANG_VK_RETURN_ON_FAIL(m_api.vkEndCommandBuffer(frameData.commandBuffer));

    VkSemaphore waitSemaphores[] = {frameData.imageAvailableSemaphore, frameData.sharedSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT};
    uint64_t waitValues[] = {0, frameData.signalValue};

    VkTimelineSemaphoreSubmitInfo timelineSubmitInfo = {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    timelineSubmitInfo.pWaitSemaphoreValues = waitValues;
    timelineSubmitInfo.waitSemaphoreValueCount = 2;

    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.pNext = &timelineSubmitInfo;
    submitInfo.waitSemaphoreCount = 2;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frameData.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &frameData.renderFinishedSemaphore;
    SLANG_VK_RETURN_ON_FAIL(m_api.vkQueueSubmit(m_queue, 1, &submitInfo, frameData.fence));

    // Present the image.
    VkPresentInfoKHR presentInfo = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frameData.renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &m_currentSwapchainImageIndex;
    SLANG_VK_RETURN_ON_FAIL(m_api.vkQueuePresentKHR(m_queue, &presentInfo));

    return SLANG_OK;
}

Result DeviceImpl::createSurface(WindowHandle windowHandle, ISurface** outSurface)
{
    SLANG_CUDA_CTX_SCOPE(this);

    RefPtr<SurfaceImpl> surface = new SurfaceImpl();
    SLANG_RETURN_ON_FAIL(surface->init(this, windowHandle));
    returnComPtr(outSurface, surface);
    return SLANG_OK;
}

} // namespace rhi::cuda

#else // SLANG_RHI_ENABLE_VULKAN

#include "cuda-base.h"
#include "cuda-device.h"

namespace rhi::cuda {

Result DeviceImpl::createSurface(WindowHandle windowHandle, ISurface** outSurface)
{
    SLANG_UNUSED(windowHandle);
    *outSurface = nullptr;
    return SLANG_E_NOT_AVAILABLE;
}

} // namespace rhi::cuda

#endif // SLANG_RHI_ENABLE_VULKAN
