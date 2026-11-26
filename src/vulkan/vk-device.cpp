#include "vk-device.h"
#include "vk-device-features.h"
#include "vk-resource-heap.h"
#include "vk-backend.h"
#include "vk-command.h"
#include "vk-buffer.h"
#include "vk-texture.h"
#include "vk-command.h"
#include "vk-fence.h"
#include "vk-query.h"
#include "vk-sampler.h"
#include "vk-shader-object-layout.h"
#include "vk-shader-object.h"
#include "vk-shader-program.h"
#include "vk-shader-table.h"
#include "vk-input-layout.h"
#include "vk-acceleration-structure.h"
#include "vk-utils.h"

#include "aftermath.h"
#include "cooperative-vector-utils.h"
#include "synthetic-resource-bindings.h"

#include "core/common.h"
#include "core/short_vector.h"
#include "core/static_vector.h"
#include "core/deferred.h"
#include "core/platform.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace rhi::vk {

static Result queryCalibratedTimestampSupport(
    VulkanApi& api,
    const std::set<std::string>& extensionNames,
    CalibratedTimestampSupport& outSupport
)
{
    outSupport = {};

    const char* calibratedTimestampExtension = nullptr;
    if (extensionNames.count(VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME))
    {
        calibratedTimestampExtension = VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME;
    }
    else if (extensionNames.count(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME))
    {
        calibratedTimestampExtension = VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME;
    }

    if (!calibratedTimestampExtension || !api.vkGetPhysicalDeviceCalibrateableTimeDomainsKHR)
    {
        return SLANG_OK;
    }

    uint32_t timeDomainCount = 0;
    SLANG_VK_RETURN_ON_FAIL(
        api.vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(api.m_physicalDevice, &timeDomainCount, nullptr)
    );
    if (timeDomainCount == 0)
    {
        return SLANG_OK;
    }

    std::vector<VkTimeDomainKHR> timeDomains(timeDomainCount);
    SLANG_VK_RETURN_ON_FAIL(
        api.vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(api.m_physicalDevice, &timeDomainCount, timeDomains.data())
    );

    const auto hasTimeDomain = [&](VkTimeDomainKHR domain)
    {
        return std::find(timeDomains.begin(), timeDomains.end(), domain) != timeDomains.end();
    };
    if (!hasTimeDomain(VK_TIME_DOMAIN_DEVICE_KHR))
    {
        return SLANG_OK;
    }

    struct Candidate
    {
        VkTimeDomainKHR vkDomain;
        CpuTimestampDomain cpuTimestampDomain;
        uint64_t cpuTimestampFrequency;
    };

    const CpuTimestampDomain cpuTimestampDomain = getCpuTimestampDomain();
    const uint64_t cpuTimestampFrequency = getCpuTimestampFrequency();
    if (cpuTimestampDomain == CpuTimestampDomain::Unknown || cpuTimestampFrequency == 0)
    {
        return SLANG_OK;
    }

    short_vector<Candidate, 3> candidates;
#if SLANG_WINDOWS_FAMILY
    if (cpuTimestampDomain == CpuTimestampDomain::QueryPerformanceCounter)
    {
        candidates.push_back({
            VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR,
            CpuTimestampDomain::QueryPerformanceCounter,
            cpuTimestampFrequency,
        });
    }
#endif
#if SLANG_LINUX_FAMILY
    if (cpuTimestampDomain == CpuTimestampDomain::ClockMonotonicRaw)
    {
        candidates.push_back({
            VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR,
            CpuTimestampDomain::ClockMonotonicRaw,
            cpuTimestampFrequency,
        });
    }
    else if (cpuTimestampDomain == CpuTimestampDomain::ClockMonotonic)
    {
        candidates.push_back({
            VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR,
            CpuTimestampDomain::ClockMonotonic,
            cpuTimestampFrequency,
        });
    }
#endif

    for (const Candidate& candidate : candidates)
    {
        if (candidate.cpuTimestampFrequency != 0 && hasTimeDomain(candidate.vkDomain))
        {
            outSupport.available = true;
            outSupport.deviceExtensionName = calibratedTimestampExtension;
            outSupport.hostTimeDomain = candidate.vkDomain;
            outSupport.cpuTimestampDomain = candidate.cpuTimestampDomain;
            outSupport.cpuTimestampFrequency = candidate.cpuTimestampFrequency;
            break;
        }
    }

    return SLANG_OK;
}

DeviceImpl::DeviceImpl() {}

DeviceImpl::~DeviceImpl()
{
    // Wait and release command buffers before releasing their device-owned heaps.
    if (m_queue)
    {
        m_queue->waitAndReleaseCommandBuffers();
    }

    // Check the device queue is valid else, we can't wait on it..
    if (m_deviceQueue.isValid())
    {
        waitForGpu();
    }

    m_shaderObjectLayoutCache = decltype(m_shaderObjectLayoutCache)();
    m_shaderCache.free();
    m_uploadHeap.release();
    m_readbackHeap.release();

    if (m_api.vkDestroySampler)
    {
        m_api.vkDestroySampler(m_device, m_defaultSampler, nullptr);
    }

    if (m_queue)
    {
        m_queue->shutdown();
        m_queue.setNull();
    }
    m_deviceQueue.destroy();
    m_bindlessDescriptorSet.setNull();

    descriptorSetAllocator.close();

    if (m_device != VK_NULL_HANDLE)
    {
        if (!m_existingDeviceHandles.handles[2])
            m_api.vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    // Also clean up when initialization failed before the device was created. In particular, the debug messenger
    // must not outlive this object when the instance is shared with the application.
    if (m_debugReportCallback != VK_NULL_HANDLE)
        m_api.vkDestroyDebugUtilsMessengerEXT(m_api.m_instance, m_debugReportCallback, nullptr);
    if (m_api.m_instance != VK_NULL_HANDLE && !m_existingDeviceHandles.handles[0])
        m_api.vkDestroyInstance(m_api.m_instance, nullptr);
}

void DeviceImpl::deferDelete(Resource* resource)
{
    SLANG_RHI_ASSERT(m_queue != nullptr);
    m_queue->deferDelete(resource);
}

VkBool32 DeviceImpl::handleDebugMessage(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData
)
{
    // Ignore: Undefined-Value-StorageImage-FormatMismatch-ImageView
    // https://docs.vulkan.org/spec/latest/chapters/textures.html#textures-format-validation
    // This happens because Slang does emit OpTypeImage with a format derived from the TextureXD<T> T type:
    // uint -> R32_UINT, uint2 -> R32G32_UINT, uint4 -> R32G32B32A32_UINT etc.
    // which might not be the actual type of the format of the image view.
    // Slang should be emitting unknown format, unless the format is explicitly set using the [[format("xxx")]]
    // attribute.
    if (pCallbackData->messageIdNumber == 20145586)
    {
        return VK_FALSE;
    }
    // Ignore: VUID-StandaloneSpirv-None-10684
    // https://vulkan.lunarg.com/doc/view/1.4.313.1/windows/antora/spec/latest/appendices/spirvenv.html#VUID-StandaloneSpirv-None-10684
    // Not quite clear why this is happening, but for now we will ignore it.
    if (pCallbackData->messageIdNumber == -1307510846)
    {
        return VK_FALSE;
    }

    // Ignore: VUID-VkWriteDescriptorSetAccelerationStructureKHR-pAccelerationStructures-03580
    // https://vulkan.lunarg.com/doc/view/1.4.321.1/windows/antora/spec/latest/chapters/descriptorsets.html#VUID-VkWriteDescriptorSetAccelerationStructureKHR-pAccelerationStructures-03580
    // Vulkan spec requires the nullDescriptor feature in VK_EXT_robustness2 to allow null-descriptors for acceleration
    // structures but currently complains even when nullDescriptor is available and enabled.
    if (pCallbackData->messageIdNumber == -1248876731)
    {
        return VK_FALSE;
    }

    // Ignore: VUID-VkShaderModuleCreateInfo-pCode-08737:
    // https://vulkan.lunarg.com/doc/view/1.4.321.1/windows/antora/spec/latest/chapters/shaders.html#VUID-VkShaderModuleCreateInfo-pCode-08737
    // This validation error is triggered by incorrect SPIR-V outputted by Slang:
    // https://github.com/shader-slang/slang/issues/11841
    if (pCallbackData->messageIdNumber == -1520283006)
    {
        return VK_FALSE;
    }

    DebugMessageType msgType = DebugMessageType::Info;

    const char* severity = "message";
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        severity = "warning";
        msgType = DebugMessageType::Warning;
    }
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        severity = "error";
        msgType = DebugMessageType::Error;
    }

    // Message can be really big (it can be assembler dump for example)
    // Use a dynamic buffer to store
    Size bufferSize = strlen(pCallbackData->pMessage) + 1024;
    std::vector<char> bufferArray;
    bufferArray.resize(bufferSize);
    char* buffer = bufferArray.data();

    snprintf(
        buffer,
        bufferSize,
        "%s: %d - %s:\n%s\n",
        severity,
        pCallbackData->messageIdNumber,
        pCallbackData->pMessageIdName,
        pCallbackData->pMessage
    );

    handleMessage(msgType, DebugMessageSource::Driver, buffer);
    return VK_FALSE;
}

VKAPI_ATTR VkBool32 VKAPI_CALL DeviceImpl::debugMessageCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
)
{
    return ((DeviceImpl*)pUserData)->handleDebugMessage(messageSeverity, messageTypes, pCallbackData);
}

void DeviceImpl::reportShaderAbortMessage()
{
    // No-op unless the full shader-abort round-trip was enabled at device creation (Feature::ShaderAbort)
    // and we still hold a device handle to query.
    if (!hasFeature(Feature::ShaderAbort) || m_device == VK_NULL_HANDLE)
        return;

    // vkGetDeviceFaultDebugInfoKHR is an error-path entry point, so load it on demand rather than
    // through the device proc table.
    auto vkGetDeviceFaultDebugInfoKHR =
        (PFN_vkGetDeviceFaultDebugInfoKHR)m_api.vkGetDeviceProcAddr(m_device, "vkGetDeviceFaultDebugInfoKHR");
    if (!vkGetDeviceFaultDebugInfoKHR)
        return;

    // Chain VkDeviceFaultShaderAbortMessageInfoKHR so the driver fills in the message produced by
    // OpAbortKHR. Use the standard two-call pattern: query the size first, then retrieve the data.
    VkDeviceFaultShaderAbortMessageInfoKHR abortInfo = {VK_STRUCTURE_TYPE_DEVICE_FAULT_SHADER_ABORT_MESSAGE_INFO_KHR};
    VkDeviceFaultDebugInfoKHR debugInfo = {VK_STRUCTURE_TYPE_DEVICE_FAULT_DEBUG_INFO_KHR};
    debugInfo.pNext = &abortInfo;

    if (vkGetDeviceFaultDebugInfoKHR(m_device, &debugInfo) != VK_SUCCESS || abortInfo.messageDataSize == 0)
        return;

    std::vector<char> buffer(abortInfo.messageDataSize);
    abortInfo.pMessageData = buffer.data();
    if (vkGetDeviceFaultDebugInfoKHR(m_device, &debugInfo) != VK_SUCCESS)
        return;

    // The message data is not guaranteed to be null-terminated; bound it to the reported size.
    std::string message(buffer.data(), buffer.size());
    handleMessage(DebugMessageType::Error, DebugMessageSource::Driver, ("Shader abort: " + message).c_str());
}

Result DeviceImpl::getNativeDeviceHandles(DeviceNativeHandles* outHandles)
{
    outHandles->handles[0].type = NativeHandleType::VkInstance;
    outHandles->handles[0].value = (uint64_t)m_api.m_instance;
    outHandles->handles[1].type = NativeHandleType::VkPhysicalDevice;
    outHandles->handles[1].value = (uint64_t)m_api.m_physicalDevice;
    outHandles->handles[2].type = NativeHandleType::VkDevice;
    outHandles->handles[2].value = (uint64_t)m_api.m_device;
    return SLANG_OK;
}

Result DeviceImpl::initVulkanInstance(
    const DeviceDesc& desc,
    const VulkanDeviceExtendedDesc* extendedDesc,
    const DebugLayerOptions& debugLayerOptions
)
{
    // Initialize Vulkan instance.
    VkInstance instance = VK_NULL_HANDLE;
    if (!desc.existingDeviceHandles.handles[0])
    {
        VkApplicationInfo applicationInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
        applicationInfo.pApplicationName = "slang-rhi";
        applicationInfo.pEngineName = "slang-rhi";
        applicationInfo.apiVersion = VK_API_VERSION_1_1;
        applicationInfo.engineVersion = 1;
        applicationInfo.applicationVersion = 1;

        // Query the available instance extensions
        uint32_t extensionCount = 0;
        m_api.vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);
        std::vector<VkExtensionProperties> extensions;
        extensions.resize(extensionCount);
        m_api.vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, extensions.data());
        std::set<std::string> extensionNames;
        for (const auto& e : extensions)
            extensionNames.emplace(e.extensionName);

        std::vector<const char*> instanceExtensions;

#if SLANG_APPLE_FAMILY
        instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif
        instanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        instanceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
        instanceExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        // Note: this extension is not yet supported by nvidia drivers, disable for now.
        // instanceExtensions.push_back("VK_GOOGLE_surfaceless_query");
#if SLANG_WINDOWS_FAMILY
        instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif SLANG_APPLE_FAMILY
        instanceExtensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#elif SLANG_LINUX_FAMILY
#if SLANG_ANDROID
        instanceExtensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#else
        if (extensionNames.count(VK_KHR_XLIB_SURFACE_EXTENSION_NAME))
            instanceExtensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
        if (extensionNames.count(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME))
            instanceExtensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#endif
#endif

        if (debugLayerOptions.isDebugLayersEnabled())
        {
            instanceExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
            instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

            if (debugLayerOptions.GPUAssistedValidation)
                instanceExtensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        }

        // Add user-provided instance extensions, skipping duplicates.
        if (extendedDesc && extendedDesc->instanceExtensionCount > 0)
        {
            std::set<std::string> existingExtensions(instanceExtensions.begin(), instanceExtensions.end());
            for (uint32_t i = 0; i < extendedDesc->instanceExtensionCount; ++i)
            {
                if (extendedDesc->instanceExtensions[i] &&
                    existingExtensions.find(extendedDesc->instanceExtensions[i]) == existingExtensions.end())
                {
                    instanceExtensions.push_back(extendedDesc->instanceExtensions[i]);
                    existingExtensions.insert(extendedDesc->instanceExtensions[i]);
                }
            }
        }

        VkInstanceCreateInfo instanceCreateInfo = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
#if SLANG_APPLE_FAMILY
        instanceCreateInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
        instanceCreateInfo.pApplicationInfo = &applicationInfo;
        instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
        instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();

        const char* layerNames[] = {nullptr};

        VkValidationFeaturesEXT validationFeatures = {};
        std::vector<VkValidationFeatureEnableEXT> enabledValidationFeatures = {};
        std::vector<VkValidationFeatureDisableEXT> disabledValidationFeatures = {};
        if (debugLayerOptions.isDebugLayersEnabled())
        {
            if (debugLayerOptions.GPUAssistedValidation)
            {
                enabledValidationFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
                enabledValidationFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT);
            }

            if (!debugLayerOptions.coreValidation)
            {
                disabledValidationFeatures.push_back(VK_VALIDATION_FEATURE_DISABLE_CORE_CHECKS_EXT);
            }
            else
            {
                // Cannot use DebugPrintf if `CoreValidation` is disabled
                if (extendedDesc && extendedDesc->enableDebugPrintf)
                    enabledValidationFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT);
            }

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

                validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
                validationFeatures.enabledValidationFeatureCount = enabledValidationFeatures.size();
                validationFeatures.pEnabledValidationFeatures = enabledValidationFeatures.data();
                validationFeatures.disabledValidationFeatureCount = disabledValidationFeatures.size();
                validationFeatures.pDisabledValidationFeatures = disabledValidationFeatures.data();
                instanceCreateInfo.pNext = &validationFeatures;
            }
        }
        uint32_t apiVersionsToTry[] = {
            // VK_API_VERSION_1_4,
            VK_API_VERSION_1_3,
            VK_API_VERSION_1_2,
            VK_API_VERSION_1_1,
            VK_API_VERSION_1_0,
        };
        for (auto apiVersion : apiVersionsToTry)
        {
            applicationInfo.apiVersion = apiVersion;
            // If r is VK_ERROR_LAYER_NOT_PRESENT, it's almost certainly
            // because the layer shared library failed to load (we check that
            // the layer is known earlier). It might, for example, be absent
            // from the system library search path, and not referenced with an
            // absolute path in VkLayer_khronos_validation.json.
            const auto r = m_api.vkCreateInstance(&instanceCreateInfo, nullptr, &instance);
            if (r == VK_SUCCESS)
            {
                m_api.m_instanceApiVersion = apiVersion;
                break;
            }
        }
    }
    else
    {
        if (desc.existingDeviceHandles.handles[0].type != NativeHandleType::VkInstance)
        {
            return SLANG_FAIL;
        }
        instance = (VkInstance)desc.existingDeviceHandles.handles[0].value;
    }
    if (!instance)
    {
        return SLANG_FAIL;
    }
    SLANG_RETURN_ON_FAIL(m_api.initInstanceProcs(instance));

    if ((desc.enableRayTracingValidation || debugLayerOptions.isDebugLayersEnabled()) &&
        m_api.vkCreateDebugUtilsMessengerEXT)
    {
        VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo = {
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT
        };
        messengerCreateInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
        messengerCreateInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        messengerCreateInfo.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messengerCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messengerCreateInfo.pfnUserCallback = &debugMessageCallback;
        messengerCreateInfo.pUserData = this;

        SLANG_VK_RETURN_ON_FAIL_REPORT(
            m_api.vkCreateDebugUtilsMessengerEXT(instance, &messengerCreateInfo, nullptr, &m_debugReportCallback),
            this
        );
    }
    return SLANG_OK;
}

Result DeviceImpl::initPhysicalDevice(const DeviceDesc& desc, BackendImpl* backend)
{
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    if (!desc.existingDeviceHandles.handles[1])
    {
        const AdapterImpl* adapter = nullptr;
        SLANG_RETURN_ON_FAIL(selectAdapter(this, backend->getAdapters(), desc, adapter));

        uint32_t physicalDeviceCount = 0;
        SLANG_VK_RETURN_ON_FAIL_REPORT(
            m_api.vkEnumeratePhysicalDevices(m_api.m_instance, &physicalDeviceCount, nullptr),
            this
        );
        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        SLANG_VK_RETURN_ON_FAIL_REPORT(
            m_api.vkEnumeratePhysicalDevices(m_api.m_instance, &physicalDeviceCount, physicalDevices.data()),
            this
        );

        // Find the physical device that matches the selected adapter UUID.
        for (uint32_t i = 0; i < physicalDeviceCount; i++)
        {
            VkPhysicalDeviceIDProperties idProps = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
            VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            props.pNext = &idProps;
            m_api.vkGetPhysicalDeviceProperties2(physicalDevices[i], &props);
            if (memcmp(adapter->m_deviceUUID, idProps.deviceUUID, VK_UUID_SIZE) == 0)
            {
                physicalDevice = physicalDevices[i];
                break;
            }
        }
    }
    else
    {
        if (desc.existingDeviceHandles.handles[1].type != NativeHandleType::VkPhysicalDevice)
        {
            return SLANG_FAIL;
        }
        physicalDevice = (VkPhysicalDevice)desc.existingDeviceHandles.handles[1].value;
    }
    if (!physicalDevice)
    {
        return SLANG_FAIL;
    }
    return m_api.initPhysicalDevice(physicalDevice);
}

Result DeviceImpl::initVulkanDevice(
    const DeviceDesc& desc,
    const VulkanDeviceExtendedDesc* extendedDesc,
    BackendImpl* backend,
    std::vector<Feature>& availableFeatures,
    std::vector<Capability>& availableCapabilities
)
{
    SLANG_RHI_ASSERT(m_api.m_instance != VK_NULL_HANDLE);

    availableFeatures.clear();
    availableCapabilities.clear();

    SLANG_RETURN_ON_FAIL(initPhysicalDevice(desc, backend));

    const bool importingDevice = bool(desc.existingDeviceHandles.handles[2]);

    // Get the API version the device is used with: the lower of the version the physical device supports and the
    // version the instance was created with. The latter is only known if slang-rhi created the instance, or if the
    // application provides it for an imported device.
    const auto majorMinor = [](uint32_t version)
    {
        return VK_MAKE_VERSION(VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), 0);
    };
    uint32_t apiVersion = majorMinor(m_api.m_deviceProperties.apiVersion);
    if (m_api.m_instanceApiVersion != 0)
        apiVersion = std::min(apiVersion, majorMinor(m_api.m_instanceApiVersion));
    if (importingDevice && extendedDesc && extendedDesc->existingDeviceApiVersion != 0)
        apiVersion = std::min(apiVersion, majorMinor(extendedDesc->existingDeviceApiVersion));

    // Query what the physical device supports.
    VulkanFeatureSet available;
    VulkanDeviceProperties deviceProperties;
    querySupportedFeatures(m_api, apiVersion, available, deviceProperties);

    // When importing a device, Vulkan cannot tell which extensions and features were enabled on it.
    // If the application describes them, only use (and report) those. Otherwise assume that everything the
    // physical device supports is enabled.
    const VkDeviceCreateInfo* existingCreateInfo = nullptr;
    int importedQueueFamilyIndex = -1;
    if (importingDevice)
    {
        if (extendedDesc)
            existingCreateInfo = static_cast<const VkDeviceCreateInfo*>(extendedDesc->existingDeviceCreateInfo);
        if (existingCreateInfo)
        {
            // Validate everything about the imported device up front, before any features are detected.
            if (existingCreateInfo->sType != VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO)
            {
                printError("VulkanDeviceExtendedDesc::existingDeviceCreateInfo must point to a VkDeviceCreateInfo.\n");
                return SLANG_E_INVALID_ARG;
            }
            // slang-rhi relies on timeline semaphores, whose feature is only queried through
            // VkPhysicalDeviceVulkan12Features.
            if (apiVersion < VK_API_VERSION_1_2)
            {
                printError("Importing a VkDevice requires Vulkan 1.2.\n");
                return SLANG_E_INVALID_ARG;
            }

            restrictToEnabledFeatures(available, *existingCreateInfo);
            if (!available.extendedFeatures.vulkan12Features.timelineSemaphore)
            {
                printError("The imported VkDevice must be created with the timelineSemaphore feature enabled.\n");
                return SLANG_E_INVALID_ARG;
            }
            importedQueueFamilyIndex = findImportedQueueFamily(m_api, *existingCreateInfo);
            if (importedQueueFamilyIndex < 0)
            {
                printError("The imported VkDevice has no queue that supports both graphics and compute.\n");
                return SLANG_E_INVALID_ARG;
            }
        }
        else
        {
            printWarning(
                "Importing a VkDevice without VulkanDeviceExtendedDesc::existingDeviceCreateInfo. Assuming that all "
                "extensions and features supported by the physical device are enabled.\n"
            );
            // Except for these, which are only used if they are known to be enabled.
            available.extendedFeatures.extendedDynamicStateFeatures.extendedDynamicState = VK_FALSE;
            available.portabilitySubsetFeatures.imageView2DOn3DImage = VK_FALSE;
        }
    }

    // Select the extensions and features to use.
    VulkanFeatureSet enabled;
    selectDeviceFeatures(available, desc, enabled);
    if (desc.enableRayTracingValidation && !enabled.extendedFeatures.rayTracingValidationFeatures.rayTracingValidation)
    {
        printWarning("Raytracing validation requested but not available.\n");
    }
    SLANG_RETURN_ON_FAIL(queryCalibratedTimestampSupport(m_api, available.extensions, m_calibratedTimestampSupport));
    if (m_calibratedTimestampSupport.available)
    {
        enabled.extensions.insert(m_calibratedTimestampSupport.deviceExtensionName);
    }

    // From here on, m_api describes what is enabled.
    m_api.m_deviceFeatures = enabled.features;
    m_api.m_extendedFeatures = enabled.extendedFeatures;
    m_api.m_rayTracingPipelineProperties = deviceProperties.rayTracingPipelineProperties;
    m_hasSubgroupSizeControl =
        enabled.hasVersion(VK_API_VERSION_1_3) || available.hasExtension(VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME);
    m_hasExtendedDynamicState = enabled.extendedFeatures.extendedDynamicStateFeatures.extendedDynamicState;
    m_supportsImageView2DOn3DImage = !enabled.hasExtension(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME) ||
                                     enabled.portabilitySubsetFeatures.imageView2DOn3DImage;
    reportDeviceFeatures(enabled, deviceProperties, availableFeatures, availableCapabilities);

    // Use a queue that was created on an imported device.
    int queueFamilyIndex =
        existingCreateInfo ? importedQueueFamilyIndex : m_api.findQueue(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
    SLANG_RHI_ASSERT(queueFamilyIndex >= 0);
    m_queueFamilyIndex = queueFamilyIndex;

#if SLANG_RHI_ENABLE_AFTERMATH
    if (desc.enableAftermath)
    {
        m_aftermathCrashDumper = AftermathCrashDumper::getOrCreate();
    }
#else
    if (desc.enableAftermath)
    {
        printWarning("Aftermath requested but not enabled in build.\n");
    }
#endif

    // Create the Vulkan device, or use the imported one.
    if (!importingDevice)
    {
        SLANG_RETURN_ON_FAIL(createVulkanDevice(desc, extendedDesc, enabled));
    }
    else
    {
        if (desc.existingDeviceHandles.handles[2].type != NativeHandleType::VkDevice)
        {
            return SLANG_FAIL;
        }
        m_device = (VkDevice)desc.existingDeviceHandles.handles[2].value;
    }
    if (!m_device)
    {
        return SLANG_FAIL;
    }
    m_api.initDerivedDeviceProperties();
    SLANG_RETURN_ON_FAIL(m_api.initDeviceProcs(m_device));

    // Report the features that also depend on device entry points.
    if (m_calibratedTimestampSupport.available && m_api.vkGetCalibratedTimestampsKHR)
    {
        availableFeatures.push_back(Feature::TimestampCalibration);
    }
    else
    {
        m_calibratedTimestampSupport = {};
    }

    if (m_api.m_extendedFeatures.pipelineBinaryFeatures.pipelineBinaries && m_api.vkGetPipelineKeyKHR &&
        m_api.vkCreatePipelineBinariesKHR && m_api.vkGetPipelineBinaryDataKHR && m_api.vkDestroyPipelineBinaryKHR &&
        m_api.vkReleaseCapturedPipelineDataKHR)
    {
        availableFeatures.push_back(Feature::PipelineCache);
    }

    return SLANG_OK;
}

Result DeviceImpl::createVulkanDevice(
    const DeviceDesc& desc,
    const VulkanDeviceExtendedDesc* extendedDesc,
    VulkanFeatureSet& enabled
)
{
    VkDeviceCreateInfo deviceCreateInfo = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    linkEnabledFeatures(enabled, deviceCreateInfo);

    float queuePriority = 0.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueCreateInfo.queueFamilyIndex = m_queueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;

    std::vector<const char*> deviceExtensions;
    for (const std::string& extension : enabled.extensions)
        deviceExtensions.push_back(extension.c_str());

#if SLANG_RHI_ENABLE_AFTERMATH
    VkDeviceDiagnosticsConfigCreateInfoNV aftermathInfo = {VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV};
    if (desc.enableAftermath)
    {
        // Enable NV_device_diagnostic_checkpoints extension to be able to use Aftermath event markers.
        deviceExtensions.push_back(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);

        // Enable NV_device_diagnostics_config extension to configure Aftermath features.
        deviceExtensions.push_back(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);

        VkDeviceDiagnosticsConfigFlagsNV aftermathFlags = 0;
        if (is_set(desc.aftermathFlags, AftermathFlags::EnableMarkers))
            aftermathFlags |= VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_AUTOMATIC_CHECKPOINTS_BIT_NV;
        if (is_set(desc.aftermathFlags, AftermathFlags::EnableResourceTracking))
            aftermathFlags |= VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV;
        if (is_set(desc.aftermathFlags, AftermathFlags::GenerateShaderDebugInfo))
            aftermathFlags |= VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV;
        if (is_set(desc.aftermathFlags, AftermathFlags::EnableShaderErrorReporting))
            aftermathFlags |= VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_ERROR_REPORTING_BIT_NV;
        aftermathInfo.flags = aftermathFlags;
        linkStruct(deviceCreateInfo, aftermathInfo);
    }
#else
    SLANG_UNUSED(desc);
#endif

    // Add user-provided device extensions, skipping duplicates.
    if (extendedDesc && extendedDesc->deviceExtensionCount > 0)
    {
        std::set<std::string> existingExtensions(deviceExtensions.begin(), deviceExtensions.end());
        for (uint32_t i = 0; i < extendedDesc->deviceExtensionCount; ++i)
        {
            if (extendedDesc->deviceExtensions[i] &&
                existingExtensions.find(extendedDesc->deviceExtensions[i]) == existingExtensions.end())
            {
                deviceExtensions.push_back(extendedDesc->deviceExtensions[i]);
                existingExtensions.insert(extendedDesc->deviceExtensions[i]);
            }
        }
    }

    deviceCreateInfo.enabledExtensionCount = uint32_t(deviceExtensions.size());
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (m_api.vkCreateDevice(m_api.m_physicalDevice, &deviceCreateInfo, nullptr, &m_device) != VK_SUCCESS)
        return SLANG_FAIL;
    return SLANG_OK;
}

Result DeviceImpl::initialize(const DeviceDesc& desc, BackendImpl* backend)
{
    SLANG_RETURN_ON_FAIL(Device::initialize(desc));

    // During shutdown we need to know if existing device handles were used.
    m_existingDeviceHandles = desc.existingDeviceHandles;

    // Process chained descs
    const VulkanDeviceExtendedDesc* extendedDesc = nullptr;
    for (const ChainedStructHeader* header = static_cast<const ChainedStructHeader*>(desc.next); header;
         header = static_cast<const ChainedStructHeader*>(header->next))
    {
        switch (header->type)
        {
        case StructType::VulkanDeviceExtendedDesc:
            extendedDesc = reinterpret_cast<const VulkanDeviceExtendedDesc*>(header);
            break;
        default:
            break;
        }
    }

    // Initialize Vulkan API and loader.
    SLANG_RETURN_ON_FAIL(m_module.init());
    SLANG_RETURN_ON_FAIL(m_api.initGlobalProcs(m_module));
    descriptorSetAllocator.m_api = &m_api;

    // Initialize Vulkan instance.
    DebugLayerOptions debugLayerOptions = getRHI()->getDebugLayerOptions();
    Result instanceResult = initVulkanInstance(desc, extendedDesc, debugLayerOptions);
    // If instance creation failed due to missing debug layers,
    // disable debug layers and try again (if they were optional).
    if (SLANG_FAILED(instanceResult) && debugLayerOptions.isDebugLayersEnabled())
    {
        bool debugLayersRequired = debugLayerOptions.required;
        printMessage(
            debugLayersRequired ? DebugMessageType::Error : DebugMessageType::Warning,
            DebugMessageSource::Layer,
            "Debug layers requested but not available.\n"
        );
        if (debugLayersRequired)
            return SLANG_FAIL;
        debugLayerOptions = {};
        instanceResult = initVulkanInstance(desc, extendedDesc, debugLayerOptions);
    }
    if (SLANG_FAILED(instanceResult))
    {
        printError("Failed to create Vulkan instance.\n");
        return instanceResult;
    }

    // Initialize Vulkan device and query available features and capabilities.
    std::vector<Feature> availableFeatures;
    std::vector<Capability> availableCapabilities;
    SLANG_RETURN_ON_FAIL(initVulkanDevice(desc, extendedDesc, backend, availableFeatures, availableCapabilities));

    VkPhysicalDeviceIDProperties idProps = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceSubgroupProperties subgroupProps = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceSubgroupSizeControlProperties subgroupSizeControlProps = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES
    };
    VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    linkStruct(props, idProps);
    linkStruct(props, subgroupProps);
    if (m_hasSubgroupSizeControl)
    {
        linkStruct(props, subgroupSizeControlProps);
    }
    m_api.vkGetPhysicalDeviceProperties2(m_api.m_physicalDevice, &props);
    const VkPhysicalDeviceProperties& basicProps = props.properties;

    // Initialize device info.
    {
        m_info.deviceType = DeviceType::Vulkan;
        m_info.apiName = "Vulkan";

        // Query adapter name.
        m_adapterName = basicProps.deviceName;
        m_info.adapterName = m_adapterName.data();

        // Query adapeter LUID.
        m_info.adapterLUID = getAdapterLUID(idProps);

        // Query timestamp frequency.
        m_info.timestampFrequency = uint64_t(1e9 / basicProps.limits.timestampPeriod);

        // Query device limits.
        DeviceLimits limits = {};
        limits.maxBufferSize = 0x80000000ull; // Assume 2GB
        limits.maxTextureDimension1D = basicProps.limits.maxImageDimension1D;
        limits.maxTextureDimension2D = basicProps.limits.maxImageDimension2D;
        limits.maxTextureDimension3D = basicProps.limits.maxImageDimension3D;
        limits.maxTextureDimensionCube = basicProps.limits.maxImageDimensionCube;
        limits.maxTextureLayers = basicProps.limits.maxImageArrayLayers;

        limits.maxVertexInputElements = basicProps.limits.maxVertexInputAttributes;
        limits.maxVertexInputElementOffset = basicProps.limits.maxVertexInputAttributeOffset;
        limits.maxVertexStreams = basicProps.limits.maxVertexInputBindings;
        limits.maxVertexStreamStride = basicProps.limits.maxVertexInputBindingStride;

        limits.maxComputeThreadsPerGroup = basicProps.limits.maxComputeWorkGroupInvocations;
        limits.maxComputeThreadGroupSize[0] = basicProps.limits.maxComputeWorkGroupSize[0];
        limits.maxComputeThreadGroupSize[1] = basicProps.limits.maxComputeWorkGroupSize[1];
        limits.maxComputeThreadGroupSize[2] = basicProps.limits.maxComputeWorkGroupSize[2];
        limits.maxComputeDispatchThreadGroups[0] = basicProps.limits.maxComputeWorkGroupCount[0];
        limits.maxComputeDispatchThreadGroups[1] = basicProps.limits.maxComputeWorkGroupCount[1];
        limits.maxComputeDispatchThreadGroups[2] = basicProps.limits.maxComputeWorkGroupCount[2];

        limits.maxViewports = basicProps.limits.maxViewports;
        limits.maxViewportDimensions[0] = basicProps.limits.maxViewportDimensions[0];
        limits.maxViewportDimensions[1] = basicProps.limits.maxViewportDimensions[1];
        limits.maxFramebufferDimensions[0] = basicProps.limits.maxFramebufferWidth;
        limits.maxFramebufferDimensions[1] = basicProps.limits.maxFramebufferHeight;
        limits.maxFramebufferDimensions[2] = basicProps.limits.maxFramebufferLayers;

        limits.maxShaderVisibleSamplers = basicProps.limits.maxPerStageDescriptorSamplers;

        if (subgroupProps.subgroupSize > 0 && (subgroupProps.supportedOperations & kWaveOpsSubgroupFeatureMask))
        {
            if (m_hasSubgroupSizeControl && subgroupSizeControlProps.minSubgroupSize > 0 &&
                subgroupSizeControlProps.maxSubgroupSize >= subgroupSizeControlProps.minSubgroupSize)
            {
                limits.minWaveSize = subgroupSizeControlProps.minSubgroupSize;
                limits.maxWaveSize = subgroupSizeControlProps.maxSubgroupSize;
            }
            else
            {
                limits.minWaveSize = subgroupProps.subgroupSize;
                limits.maxWaveSize = subgroupProps.subgroupSize;
            }
        }

        m_info.limits = limits;
    }

    // Initialize features & capabilities.
    bool isSoftwareDevice = (basicProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_OTHER) ||
                            (basicProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU);
    addFeature(isSoftwareDevice ? Feature::SoftwareDevice : Feature::HardwareDevice);
    addFeature(Feature::ParameterBlock);
    addFeature(Feature::Rasterization);
    addFeature(Feature::CombinedTextureSampler);
    addFeature(Feature::TimestampQuery);
    addFeature(Feature::ResourceHeaps);
    addFeature(Feature::ResourceAliasing);
    for (auto feature : availableFeatures)
    {
        addFeature(feature);
    }

    addCapability(Capability::spirv);
    for (auto capability : availableCapabilities)
    {
        addCapability(capability);
    }

    // Derive approximate DX12 shader model.
    {
        Feature featureTable[] = {
            // SM 6.6
            Feature::SM_6_6,
            Feature::AtomicFloat,
            Feature::AtomicInt64,
            // SM 6.5
            Feature::SM_6_5,
            Feature::RayQuery,
            Feature::MeshShader,
            // SM 6.4
            Feature::SM_6_4,
            Feature::FragmentShadingRate,
            // TODO: Check VK_NV_shader_image_footprint?
            // Feature::SamplerFeedback,
            // SM 6.3
            Feature::SM_6_3,
            Feature::RayTracing,
            // SM 6.2
            Feature::SM_6_2,
            Feature::Half,
            // SM 6.1
            Feature::SM_6_1,
            Feature::Barycentrics,
            Feature::MultiView,
            // SM 6.0
            Feature::SM_6_0,
            Feature::Int64,
            Feature::WaveOps,
        };

        for (int i = SLANG_COUNT_OF(featureTable) - 1; i >= 0; --i)
        {
            Feature feature = featureTable[i];
            if (int(feature) >= int(Feature::SM_6_0) && int(feature) <= int(Feature::SM_6_10))
            {
                addFeature(feature);
            }
            else if (!hasFeature(feature))
            {
                break;
            }
        }
    }

    // Initialize format support table.
    for (size_t formatIndex = 0; formatIndex < size_t(Format::_Count); ++formatIndex)
    {
        Format format = Format(formatIndex);
        FormatSupport formatSupport = FormatSupport::None;

#define UPDATE_FLAGS(vkFlags, vkMask, formatSupportFlags)                                                              \
    formatSupport |= (vkFlags & vkMask) ? formatSupportFlags : FormatSupport::None;

        VkFormat vkFormat = getVkFormat(format);

        VkFormatProperties2 props2 = {VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};
        m_api.vkGetPhysicalDeviceFormatProperties2(m_api.m_physicalDevice, vkFormat, &props2);

        VkFormatFeatureFlags bf = props2.formatProperties.bufferFeatures;
        VkFormatFeatureFlags ltf = props2.formatProperties.linearTilingFeatures;
        VkFormatFeatureFlags otf = props2.formatProperties.optimalTilingFeatures;

        UPDATE_FLAGS(ltf, VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT, FormatSupport::CopySource);
        UPDATE_FLAGS(ltf, VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT, FormatSupport::CopyDestination);

        if (otf)
        {
            formatSupport |= FormatSupport::Texture;

            UPDATE_FLAGS(otf, VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT, FormatSupport::RenderTarget);
            UPDATE_FLAGS(otf, VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT, FormatSupport::Blendable);
            UPDATE_FLAGS(otf, VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT, FormatSupport::DepthStencil);

            UPDATE_FLAGS(otf, VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT, FormatSupport::ShaderLoad);
            UPDATE_FLAGS(otf, VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT, FormatSupport::ShaderSample);
            UPDATE_FLAGS(
                otf,
                VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT,
                FormatSupport::ShaderUavLoad | FormatSupport::ShaderUavStore
            );
            UPDATE_FLAGS(otf, VK_FORMAT_FEATURE_2_STORAGE_IMAGE_ATOMIC_BIT, FormatSupport::ShaderAtomic);

            VkPhysicalDeviceImageFormatInfo2 imageInfo = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
            imageInfo.format = vkFormat;
            imageInfo.type = VK_IMAGE_TYPE_2D;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.flags = 0; // TODO: kinda needed, but unknown here

            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            if (is_set(formatSupport, FormatSupport::Texture))
                imageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
            if (is_set(formatSupport, FormatSupport::RenderTarget))
                imageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            if (is_set(formatSupport, FormatSupport::DepthStencil))
                imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

            VkImageFormatProperties2 imageProps = {VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
            if (m_api.vkGetPhysicalDeviceImageFormatProperties2(m_api.m_physicalDevice, &imageInfo, &imageProps) !=
                VK_SUCCESS)
            {
                m_formatSupport[formatIndex] = FormatSupport::None;
                continue;
            }

            if (imageProps.imageFormatProperties.sampleCounts > 1)
            {
                formatSupport |= FormatSupport::Multisampling;
                if (imageProps.imageFormatProperties.sampleCounts & VK_SAMPLE_COUNT_1_BIT)
                {
                    formatSupport |= FormatSupport::Resolvable;
                }
            }
        }
        if (bf)
        {
            formatSupport |= FormatSupport::Buffer;

            UPDATE_FLAGS(bf, VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT, FormatSupport::ShaderLoad);
            UPDATE_FLAGS(
                bf,
                VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT,
                FormatSupport::ShaderUavLoad | FormatSupport::ShaderUavStore
            );
            UPDATE_FLAGS(bf, VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_ATOMIC_BIT, FormatSupport::ShaderAtomic);

            UPDATE_FLAGS(bf, VK_FORMAT_FEATURE_2_VERTEX_BUFFER_BIT, FormatSupport::VertexBuffer);
            if (format == Format::R32Uint || format == Format::R16Uint)
            {
                formatSupport |= FormatSupport::IndexBuffer;
            }
        }

#undef UPDATE_FLAGS

        m_formatSupport[formatIndex] = formatSupport;
    }

    // Initialize slang context.
    SLANG_RETURN_ON_FAIL(m_slangContext.initialize(
        desc.slang,
        SLANG_SPIRV,
        nullptr,
        getCapabilities(),
        std::array{slang::PreprocessorMacroDesc{"__VULKAN__", "1"}}
    ));

    // Create default sampler.
    {
        VkSamplerCreateInfo samplerInfo = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        SLANG_VK_RETURN_ON_FAIL_REPORT(m_api.vkCreateSampler(m_device, &samplerInfo, nullptr, &m_defaultSampler), this);
    }

    // Create bindless descriptor set if needed.
    if (hasFeature(Feature::Bindless))
    {
        m_bindlessDescriptorSet = new BindlessDescriptorSet(this, desc.bindless);
        SLANG_RETURN_ON_FAIL(m_bindlessDescriptorSet->initialize());
    }

    {
        VkQueue queue;
        m_api.vkGetDeviceQueue(m_device, m_queueFamilyIndex, 0, &queue);
        SLANG_RETURN_ON_FAIL(m_deviceQueue.init(m_api, queue, m_queueFamilyIndex));
    }

    m_queue = new CommandQueueImpl(this, QueueType::Graphics);
    m_queue->init(m_deviceQueue.getQueue(), m_queueFamilyIndex);

    SLANG_RETURN_ON_FAIL(checkRequiredFeatures(desc));

    return SLANG_OK;
}

void DeviceImpl::waitForGpu()
{
    m_deviceQueue.flushAndWait();
}

Result DeviceImpl::getQueue(QueueType type, ICommandQueue** outQueue)
{
    if (type != QueueType::Graphics)
    {
        return SLANG_E_INVALID_ARG;
    }
    returnComPtrCopy(outQueue, m_queue);
    return SLANG_OK;
}

Result DeviceImpl::readBuffer(IBuffer* buffer, Offset offset, Size size, void* outData)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    if (offset + size > bufferImpl->m_desc.size)
    {
        return SLANG_FAIL;
    }

    // create staging buffer (prefer cached memory, the CPU reads from it)
    VKBufferHandleRAII staging;

    SLANG_RETURN_ON_FAIL(staging.init(
        m_api,
        size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        0,
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT
    ));

    // Copy from real buffer to staging buffer
    VkCommandBuffer commandBuffer = m_deviceQueue.getCommandBuffer();

    VkBufferMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = calcAccessFlags(bufferImpl->m_desc.defaultState);
    barrier.dstAccessMask = calcAccessFlags(ResourceState::CopySource);
    barrier.buffer = bufferImpl->m_buffer.m_buffer;
    barrier.offset = 0;
    barrier.size = bufferImpl->m_desc.size;

    VkPipelineStageFlags srcStageFlags =
        calcPipelineStageFlags(m_api.m_supportedShaderStageFlags, bufferImpl->m_desc.defaultState, true);
    VkPipelineStageFlags dstStageFlags =
        calcPipelineStageFlags(m_api.m_supportedShaderStageFlags, ResourceState::CopySource, false);

    m_api.vkCmdPipelineBarrier(
        commandBuffer,
        srcStageFlags,
        dstStageFlags,
        VkDependencyFlags(0),
        0,
        nullptr,
        1,
        &barrier,
        0,
        nullptr
    );

    VkBufferCopy copyInfo = {};
    copyInfo.size = size;
    copyInfo.srcOffset = offset;
    m_api.vkCmdCopyBuffer(commandBuffer, bufferImpl->m_buffer.m_buffer, staging.m_buffer, 1, &copyInfo);

    std::swap(barrier.srcAccessMask, barrier.dstAccessMask);
    std::swap(srcStageFlags, dstStageFlags);

    m_api.vkCmdPipelineBarrier(
        commandBuffer,
        srcStageFlags,
        dstStageFlags,
        VkDependencyFlags(0),
        0,
        nullptr,
        1,
        &barrier,
        0,
        nullptr
    );

    m_deviceQueue.flushAndWait();

    // Write out the data from the buffer
    void* mappedData = nullptr;
    SLANG_RETURN_ON_FAIL(m_api.vkMapMemory(m_device, staging.m_memory, 0, size, 0, &mappedData));

    std::memcpy(outData, mappedData, size);
    m_api.vkUnmapMemory(m_device, staging.m_memory);

    return SLANG_OK;
}

Result DeviceImpl::getAccelerationStructureSizes(
    const AccelerationStructureBuildDesc& desc,
    AccelerationStructureSizes* outSizes
)
{
    if (!m_api.vkGetAccelerationStructureBuildSizesKHR)
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    AccelerationStructureBuildDescConverter converter;
    SLANG_RETURN_ON_FAIL(converter.convert(desc, m_debugCallback));
    m_api.vkGetAccelerationStructureBuildSizesKHR(
        m_api.m_device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &converter.buildInfo,
        converter.primitiveCounts.data(),
        &sizeInfo
    );
    outSizes->accelerationStructureSize = sizeInfo.accelerationStructureSize;
    outSizes->scratchSize = sizeInfo.buildScratchSize;
    outSizes->updateScratchSize = sizeInfo.updateScratchSize;
    return SLANG_OK;
}

Result DeviceImpl::getMicromapSizes(const MicromapBuildDesc& desc, MicromapSizes* outSizes)
{
    if (!m_api.vkGetMicromapBuildSizesEXT)
        return SLANG_E_NOT_AVAILABLE;
    MicromapBuildDescConverter converter;
    SLANG_RETURN_ON_FAIL(converter.convert(desc));
    VkMicromapBuildSizesInfoEXT info = {VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT};
    m_api.vkGetMicromapBuildSizesEXT(
        m_device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &converter.buildInfo,
        &info
    );
    outSizes->micromapSize = info.micromapSize;
    outSizes->scratchSize = info.buildScratchSize;
    return SLANG_OK;
}

Result DeviceImpl::getClusterOperationSizes(const ClusterOperationParams& params, ClusterOperationSizes* outSizes)
{
    if (!m_api.vkGetClusterAccelerationStructureBuildSizesNV)
    {
        return SLANG_E_NOT_AVAILABLE;
    }

    VkClusterAccelerationStructureClustersBottomLevelInputNV bottomLevelInput;
    VkClusterAccelerationStructureTriangleClusterInputNV triangleClusterInput;
    VkClusterAccelerationStructureMoveObjectsInputNV moveObjectsInput;
    VkClusterAccelerationStructureInputInfoNV info =
        translateClusterOperationParams(params, bottomLevelInput, triangleClusterInput, moveObjectsInput);
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    m_api.vkGetClusterAccelerationStructureBuildSizesNV(m_device, &info, &sizeInfo);

    outSizes->resultSize = sizeInfo.accelerationStructureSize;
    outSizes->scratchSize = sizeInfo.buildScratchSize;

    return SLANG_OK;
}

Result DeviceImpl::createAccelerationStructure(
    const AccelerationStructureDesc& desc,
    IAccelerationStructure** outAccelerationStructure
)
{
    if (!m_api.vkCreateAccelerationStructureKHR)
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    RefPtr<AccelerationStructureImpl> result = new AccelerationStructureImpl(this, desc);
    BufferDesc bufferDesc = {};
    bufferDesc.size = desc.size;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    bufferDesc.usage = BufferUsage::AccelerationStructure;
    bufferDesc.defaultState = ResourceState::AccelerationStructureRead;
    RefPtr<BufferImpl> buffer;
    SLANG_RETURN_ON_FAIL(createBuffer(bufferDesc, nullptr, (IBuffer**)buffer.writeRef()));
    result->m_buffer = buffer;
    VkAccelerationStructureCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    VkAccelerationStructureMotionInfoNV motionInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MOTION_INFO_NV};
    createInfo.buffer = result->m_buffer->m_buffer.m_buffer;
    createInfo.offset = 0;
    createInfo.size = desc.size;
    createInfo.type = translateAccelerationStructureKind(desc.kind);
    if (is_set(desc.flags, AccelerationStructureBuildFlags::CreateMotion))
    {
        createInfo.createFlags |= VK_ACCELERATION_STRUCTURE_CREATE_MOTION_BIT_NV;
        if (desc.motionInfo.enabled)
        {
            if (desc.motionInfo.maxInstances == 0)
            {
                return SLANG_E_INVALID_ARG;
            }

            motionInfo.pNext = nullptr;
            motionInfo.maxInstances = desc.motionInfo.maxInstances;

            // VK reserved this field but it isn't used yet.
            motionInfo.flags = 0;

            createInfo.pNext = &motionInfo;
        }
    }
    SLANG_VK_RETURN_ON_FAIL_REPORT(
        m_api.vkCreateAccelerationStructureKHR(m_api.m_device, &createInfo, nullptr, &result->m_vkHandle),
        this
    );
    returnComPtr(outAccelerationStructure, result);
    return SLANG_OK;
}

Result DeviceImpl::createMicromap(const MicromapDesc& desc, IMicromap** outMicromap)
{
    if (!m_api.vkCreateMicromapEXT)
        return SLANG_E_NOT_AVAILABLE;
    RefPtr<MicromapImpl> result = new MicromapImpl(this, desc);
    BufferDesc bufferDesc = {};
    bufferDesc.size = desc.size;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    bufferDesc.usage = BufferUsage::MicromapStorage;
    bufferDesc.defaultState = ResourceState::MicromapRead;
    RefPtr<BufferImpl> buffer;
    SLANG_RETURN_ON_FAIL(createBuffer(bufferDesc, nullptr, (IBuffer**)buffer.writeRef()));
    result->m_buffer = buffer;
    VkMicromapCreateInfoEXT createInfo = {VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT};
    createInfo.buffer = result->m_buffer->m_buffer.m_buffer;
    createInfo.size = desc.size;
    createInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    SLANG_VK_RETURN_ON_FAIL(m_api.vkCreateMicromapEXT(m_device, &createInfo, nullptr, &result->m_vkHandle));
    returnComPtr(outMicromap, result);
    return SLANG_OK;
}

void DeviceImpl::_transitionImageLayout(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkFormat format,
    const TextureDesc& desc,
    VkImageLayout oldLayout,
    VkImageLayout newLayout
)
{
    if (oldLayout == newLayout)
        return;

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;

    barrier.subresourceRange.aspectMask = getAspectMaskFromFormat(format);

    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = desc.mipCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    barrier.srcAccessMask = calcAccessFlagsFromImageLayout(oldLayout);
    barrier.dstAccessMask = calcAccessFlagsFromImageLayout(newLayout);

    VkPipelineStageFlags sourceStage = calcPipelineStageFlagsFromImageLayout(oldLayout);
    VkPipelineStageFlags destinationStage = calcPipelineStageFlagsFromImageLayout(newLayout);

    m_api.vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

uint32_t DeviceImpl::getQueueFamilyIndex(QueueType queueType)
{
    switch (queueType)
    {
    case QueueType::Graphics:
    default:
        return m_queueFamilyIndex;
    }
}

void DeviceImpl::_transitionImageLayout(
    VkImage image,
    VkFormat format,
    const TextureDesc& desc,
    VkImageLayout oldLayout,
    VkImageLayout newLayout
)
{
    VkCommandBuffer commandBuffer = m_deviceQueue.getCommandBuffer();
    _transitionImageLayout(commandBuffer, image, format, desc, oldLayout, newLayout);
}

void DeviceImpl::_labelObject(uint64_t object, VkObjectType objectType, const char* label)
{
    if (label && m_api.vkSetDebugUtilsObjectNameEXT)
    {
        VkDebugUtilsObjectNameInfoEXT nameInfo = {VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
        nameInfo.objectHandle = object;
        nameInfo.objectType = objectType;
        nameInfo.pObjectName = label;
        m_api.vkSetDebugUtilsObjectNameEXT(m_api.m_device, &nameInfo);
    }
}

Result DeviceImpl::createResourceHeap(const ResourceHeapDesc& desc, IResourceHeap** outHeap)
{
    SLANG_RETURN_ON_FAIL(validateResourceHeapDesc(this, desc));
    RefPtr<ResourceHeapImpl> heap = new ResourceHeapImpl(this, desc);
    SLANG_RETURN_ON_FAIL(heap->init());
    returnComPtr(outHeap, heap);
    return SLANG_OK;
}

Result DeviceImpl::getBufferMemoryRequirements(const BufferDesc& desc_, ResourceMemoryRequirements* outRequirements)
{
    resetResourceMemoryRequirements(outRequirements);
    BufferDesc desc = fixupBufferDesc(desc_);
    VkBufferUsageFlags usage = getBufferUsageFlags(m_api, desc);
    VkBuffer buffer = VK_NULL_HANDLE;
    SLANG_RETURN_ON_FAIL(createVkBuffer(m_api, desc.size, usage, 0, &buffer));
    VkMemoryRequirements reqs = {};
    VkMemoryDedicatedRequirements dedicatedReqs = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
    if (m_api.vkGetBufferMemoryRequirements2)
    {
        VkBufferMemoryRequirementsInfo2 info = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2};
        info.buffer = buffer;
        VkMemoryRequirements2 reqs2 = {VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
        reqs2.pNext = &dedicatedReqs;
        m_api.vkGetBufferMemoryRequirements2(m_device, &info, &reqs2);
        reqs = reqs2.memoryRequirements;
    }
    else
    {
        m_api.vkGetBufferMemoryRequirements(m_device, buffer, &reqs);
    }
    m_api.vkDestroyBuffer(m_device, buffer, nullptr);

    const Size granularity = max<Size>(m_api.m_deviceProperties.limits.bufferImageGranularity, 1);
    outRequirements->size = math::calcAligned((Size)reqs.size, granularity);
    outRequirements->alignment = max<Size>((Size)reqs.alignment, granularity);
    outRequirements->heapAlignment = 1;
    outRequirements->memoryType = desc.memoryType;
    outRequirements->usage = getResourceHeapUsage(desc);
    if (is_set(desc.usage, BufferUsage::Shared) || dedicatedReqs.requiresDedicatedAllocation)
        outRequirements->flags |= ResourceMemoryRequirementFlags::RequiresDedicatedAllocation;
    if (dedicatedReqs.prefersDedicatedAllocation)
        outRequirements->flags |= ResourceMemoryRequirementFlags::PrefersDedicatedAllocation;
    setResourceHeapCompatibility(*outRequirements, this, reqs.memoryTypeBits);
    return SLANG_OK;
}

Result DeviceImpl::checkTexturePlacementSupported(const TextureDesc& desc)
{
    if (desc.memoryType != MemoryType::DeviceLocal)
        return SLANG_E_NOT_AVAILABLE;
    if (desc.type == TextureType::Texture3D && is_set(desc.usage, TextureUsage::RenderTarget) &&
        !m_supportsImageView2DOn3DImage)
        return SLANG_E_NOT_AVAILABLE;
    return SLANG_OK;
}

Result DeviceImpl::getTextureMemoryRequirements(const TextureDesc& desc_, ResourceMemoryRequirements* outRequirements)
{
    resetResourceMemoryRequirements(outRequirements);
    TextureDesc desc = fixupTextureDesc(desc_);
    SLANG_RETURN_ON_FAIL(checkTexturePlacementSupported(desc));
    VkImageCreateInfo imageInfo = {};
    SLANG_RETURN_ON_FAIL(getVkImageCreateInfo(desc, true, &imageInfo));

    VkExternalMemoryImageCreateInfo externalMemoryImageCreateInfo = {
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO
    };
    if (is_set(desc.usage, TextureUsage::Shared))
    {
#if SLANG_WINDOWS_FAMILY
        externalMemoryImageCreateInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
        externalMemoryImageCreateInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
        imageInfo.pNext = &externalMemoryImageCreateInfo;
    }

    VkImage image = VK_NULL_HANDLE;
    SLANG_VK_RETURN_ON_FAIL_REPORT(m_api.vkCreateImage(m_device, &imageInfo, nullptr, &image), this);
    VkMemoryRequirements reqs = {};
    VkMemoryDedicatedRequirements dedicatedReqs = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
    if (m_api.vkGetImageMemoryRequirements2)
    {
        VkImageMemoryRequirementsInfo2 info = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2};
        info.image = image;
        VkMemoryRequirements2 reqs2 = {VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
        reqs2.pNext = &dedicatedReqs;
        m_api.vkGetImageMemoryRequirements2(m_device, &info, &reqs2);
        reqs = reqs2.memoryRequirements;
    }
    else
    {
        m_api.vkGetImageMemoryRequirements(m_device, image, &reqs);
    }
    m_api.vkDestroyImage(m_device, image, nullptr);

    const Size granularity = max<Size>(m_api.m_deviceProperties.limits.bufferImageGranularity, 1);
    outRequirements->size = math::calcAligned((Size)reqs.size, granularity);
    outRequirements->alignment = max<Size>((Size)reqs.alignment, granularity);
    outRequirements->heapAlignment = 1;
    outRequirements->memoryType = desc.memoryType;
    outRequirements->usage = getResourceHeapUsage(desc);
    if (is_set(desc.usage, TextureUsage::Shared) || dedicatedReqs.requiresDedicatedAllocation)
        outRequirements->flags |= ResourceMemoryRequirementFlags::RequiresDedicatedAllocation;
    if (dedicatedReqs.prefersDedicatedAllocation)
        outRequirements->flags |= ResourceMemoryRequirementFlags::PrefersDedicatedAllocation;
    setResourceHeapCompatibility(*outRequirements, this, reqs.memoryTypeBits);
    return SLANG_OK;
}

Result DeviceImpl::getTextureAllocationInfo(const TextureDesc& desc_, Size* outSize, Size* outAlignment)
{
    TextureDesc desc = fixupTextureDesc(desc_);

    VkImageCreateInfo imageInfo = {};
    SLANG_RETURN_ON_FAIL(getVkImageCreateInfo(desc, false, &imageInfo));

    VkImage image;
    SLANG_VK_RETURN_ON_FAIL_REPORT(m_api.vkCreateImage(m_device, &imageInfo, nullptr, &image), this);

    VkMemoryRequirements memRequirements;
    m_api.vkGetImageMemoryRequirements(m_device, image, &memRequirements);

    *outSize = (Size)memRequirements.size;
    *outAlignment = (Size)memRequirements.alignment;

    m_api.vkDestroyImage(m_device, image, nullptr);
    return SLANG_OK;
}

Result DeviceImpl::getTextureRowAlignment(Format format, Size* outAlignment)
{
    Size blockSize = getFormatInfo(format).blockSizeInBytes;
    if (blockSize == 0)
        blockSize = 1;

    switch (format)
    {
    case Format::D16Unorm:
    case Format::D32Float:
    case Format::D32FloatS8Uint:
        *outAlignment = math::calcAligned(4, blockSize);
        break;
    default:
        // VkBufferImageCopy expresses bufferRowLength in texels, so the byte
        // row pitch must represent a whole number of format blocks.
        *outAlignment = blockSize;
        break;
    }
    return SLANG_OK;
}

Result DeviceImpl::getTextureBufferOffsetAlignment(Format format, Size* outAlignment)
{
    const FormatInfo& formatInfo = getFormatInfo(format);
    if (formatInfo.blockSizeInBytes == 0)
        return SLANG_E_INVALID_ARG;
    *outAlignment = formatInfo.kind == FormatKind::DepthStencil ? 4 : formatInfo.blockSizeInBytes;
    return SLANG_OK;
}

Result DeviceImpl::isCooperativeMatrixSupported(const CooperativeMatrixDesc& desc, bool* outSupported)
{
    if (!outSupported)
        return SLANG_E_INVALID_ARG;

    *outSupported = false;

    if (desc.m == 0 || desc.n == 0 || desc.k == 0)
        return SLANG_OK;

    if (!m_api.m_extendedFeatures.cooperativeMatrix1Features.cooperativeMatrix ||
        !m_api.vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR)
    {
        return SLANG_OK;
    }

    if (!m_cooperativeMatrixPropertiesInitialized)
    {
        m_cooperativeMatrixPropertiesInitialized = true;
        m_cooperativeMatrixFixedProperties.clear();
        m_cooperativeMatrixFlexibleProperties.clear();

        uint32_t count = 0;
        m_api.vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(m_api.m_physicalDevice, &count, nullptr);
        if (count == 0)
            return SLANG_OK;

        std::vector<VkCooperativeMatrixPropertiesKHR> props(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            props[i].sType = VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR;
            props[i].pNext = nullptr;
        }

        m_api.vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(m_api.m_physicalDevice, &count, props.data());

        for (uint32_t i = 0; i < count; ++i)
        {
            CooperativeMatrixComponentType aType;
            CooperativeMatrixComponentType bType;
            CooperativeMatrixComponentType cType;
            CooperativeMatrixComponentType resultType;
            CooperativeMatrixScope scope;
            if (!translateCooperativeMatrixComponentType(props[i].AType, aType) ||
                !translateCooperativeMatrixComponentType(props[i].BType, bType) ||
                !translateCooperativeMatrixComponentType(props[i].CType, cType) ||
                !translateCooperativeMatrixComponentType(props[i].ResultType, resultType) ||
                !translateCooperativeMatrixScope(props[i].scope, scope))
            {
                continue;
            }

            CooperativeMatrixDesc fixedProp = {};
            fixedProp.m = props[i].MSize;
            fixedProp.n = props[i].NSize;
            fixedProp.k = props[i].KSize;
            fixedProp.aType = aType;
            fixedProp.bType = bType;
            fixedProp.cType = cType;
            fixedProp.resultType = resultType;
            fixedProp.scope = scope;
            m_cooperativeMatrixFixedProperties.push_back(fixedProp);
        }

        if (m_api.m_extendedFeatures.cooperativeMatrix2Features.cooperativeMatrixFlexibleDimensions &&
            m_api.vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV)
        {
            uint32_t flexCount = 0;
            m_api.vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV(
                m_api.m_physicalDevice,
                &flexCount,
                nullptr
            );
            if (flexCount > 0)
            {
                std::vector<VkCooperativeMatrixFlexibleDimensionsPropertiesNV> flexProps(
                    flexCount,
                    {VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_FLEXIBLE_DIMENSIONS_PROPERTIES_NV}
                );
                m_api.vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV(
                    m_api.m_physicalDevice,
                    &flexCount,
                    flexProps.data()
                );
                for (uint32_t i = 0; i < flexCount; ++i)
                {
                    CooperativeMatrixComponentType aType;
                    CooperativeMatrixComponentType bType;
                    CooperativeMatrixComponentType cType;
                    CooperativeMatrixComponentType resultType;
                    CooperativeMatrixScope scope;
                    if (!translateCooperativeMatrixComponentType(flexProps[i].AType, aType) ||
                        !translateCooperativeMatrixComponentType(flexProps[i].BType, bType) ||
                        !translateCooperativeMatrixComponentType(flexProps[i].CType, cType) ||
                        !translateCooperativeMatrixComponentType(flexProps[i].ResultType, resultType) ||
                        !translateCooperativeMatrixScope(flexProps[i].scope, scope))
                    {
                        continue;
                    }

                    if (flexProps[i].MGranularity == 0 || flexProps[i].NGranularity == 0 ||
                        flexProps[i].KGranularity == 0)
                    {
                        continue;
                    }

                    SLANG_RHI_ASSERT(math::isPowerOf2(flexProps[i].MGranularity));
                    SLANG_RHI_ASSERT(math::isPowerOf2(flexProps[i].NGranularity));
                    SLANG_RHI_ASSERT(math::isPowerOf2(flexProps[i].KGranularity));

                    CooperativeMatrixFlexibleProperty flexProp = {};
                    flexProp.mGranularity = flexProps[i].MGranularity;
                    flexProp.nGranularity = flexProps[i].NGranularity;
                    flexProp.kGranularity = flexProps[i].KGranularity;
                    flexProp.aType = aType;
                    flexProp.bType = bType;
                    flexProp.cType = cType;
                    flexProp.resultType = resultType;
                    flexProp.scope = scope;
                    m_cooperativeMatrixFlexibleProperties.push_back(flexProp);
                }
            }
        }
    }

    for (const auto& prop : m_cooperativeMatrixFixedProperties)
    {
        if (prop.scope != desc.scope)
            continue;
        if (prop.aType != desc.aType || prop.bType != desc.bType || prop.cType != desc.cType ||
            prop.resultType != desc.resultType)
            continue;
        if (prop.m == desc.m && prop.n == desc.n && prop.k == desc.k)
        {
            *outSupported = true;
            return SLANG_OK;
        }
    }

    for (const auto& prop : m_cooperativeMatrixFlexibleProperties)
    {
        if (prop.scope != desc.scope)
            continue;
        if (prop.aType != desc.aType || prop.bType != desc.bType || prop.cType != desc.cType ||
            prop.resultType != desc.resultType)
            continue;
        if (prop.mGranularity == 0 || prop.nGranularity == 0 || prop.kGranularity == 0)
            continue;
        if (desc.m < prop.mGranularity || desc.n < prop.nGranularity || desc.k < prop.kGranularity)
            continue;
        if ((desc.m & (prop.mGranularity - 1)) != 0 || (desc.n & (prop.nGranularity - 1)) != 0 ||
            (desc.k & (prop.kGranularity - 1)) != 0)
            continue;
        *outSupported = true;
        return SLANG_OK;
    }

    return SLANG_OK;
}

Result DeviceImpl::getCooperativeVectorProperties(CooperativeVectorProperties* properties, uint32_t* propertiesCount)
{
    if (!m_api.m_extendedFeatures.cooperativeVectorFeatures.cooperativeVector ||
        !m_api.vkGetPhysicalDeviceCooperativeVectorPropertiesNV)
        return SLANG_E_NOT_AVAILABLE;

    if (m_cooperativeVectorProperties.empty())
    {
        uint32_t vkPropertyCount = 0;
        m_api.vkGetPhysicalDeviceCooperativeVectorPropertiesNV(m_api.m_physicalDevice, &vkPropertyCount, nullptr);
        std::vector<VkCooperativeVectorPropertiesNV> vkProperties(
            vkPropertyCount,
            {VK_STRUCTURE_TYPE_COOPERATIVE_VECTOR_PROPERTIES_NV}
        );
        SLANG_VK_RETURN_ON_FAIL_REPORT(
            m_api.vkGetPhysicalDeviceCooperativeVectorPropertiesNV(
                m_api.m_physicalDevice,
                &vkPropertyCount,
                vkProperties.data()
            ),
            this
        );
        for (const auto& vkProps : vkProperties)
        {
            CooperativeVectorProperties props;
            props.inputType = translateCooperativeVectorComponentType(vkProps.inputType);
            props.inputInterpretation = translateCooperativeVectorComponentType(vkProps.inputInterpretation);
            props.matrixInterpretation = translateCooperativeVectorComponentType(vkProps.matrixInterpretation);
            props.biasInterpretation = translateCooperativeVectorComponentType(vkProps.biasInterpretation);
            props.resultType = translateCooperativeVectorComponentType(vkProps.resultType);
            props.transpose = vkProps.transpose;
            m_cooperativeVectorProperties.push_back(props);
        }
    }

    return Device::getCooperativeVectorProperties(properties, propertiesCount);
}

Result DeviceImpl::getCooperativeVectorMatrixSize(
    uint32_t rowCount,
    uint32_t colCount,
    CooperativeVectorComponentType componentType,
    CooperativeVectorMatrixLayout layout,
    size_t rowColumnStride,
    size_t* outSize
)
{
    if (!m_api.m_extendedFeatures.cooperativeVectorFeatures.cooperativeVector ||
        !m_api.vkConvertCooperativeVectorMatrixNV)
        return SLANG_E_NOT_AVAILABLE;

    if (rowColumnStride == 0)
    {
        rowColumnStride = getTightRowColumnStride(rowCount, colCount, componentType, layout);
    }

    VkConvertCooperativeVectorMatrixInfoNV info = {VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV};
    info.pDstSize = outSize;
    info.srcComponentType = translateCooperativeVectorComponentType(componentType);
    info.dstComponentType = translateCooperativeVectorComponentType(componentType);
    info.numRows = rowCount;
    info.numColumns = colCount;
    info.srcLayout = translateCooperativeVectorMatrixLayout(layout);
    info.srcStride = rowColumnStride;
    info.dstLayout = translateCooperativeVectorMatrixLayout(layout);
    info.dstStride = rowColumnStride;
    SLANG_VK_RETURN_ON_FAIL_REPORT(m_api.vkConvertCooperativeVectorMatrixNV(m_api.m_device, &info), this);
    return SLANG_OK;
}

Result DeviceImpl::convertCooperativeVectorMatrix(
    void* dstBuffer,
    size_t dstBufferSize,
    const CooperativeVectorMatrixDesc* dstDescs,
    const void* srcBuffer,
    size_t srcBufferSize,
    const CooperativeVectorMatrixDesc* srcDescs,
    uint32_t matrixCount
)
{
    if (!m_api.m_extendedFeatures.cooperativeVectorFeatures.cooperativeVector ||
        !m_api.vkConvertCooperativeVectorMatrixNV)
        return SLANG_E_NOT_AVAILABLE;

    for (uint32_t i = 0; i < matrixCount; ++i)
    {
        const CooperativeVectorMatrixDesc& dstDesc = dstDescs[i];
        const CooperativeVectorMatrixDesc& srcDesc = srcDescs[i];
        VkConvertCooperativeVectorMatrixInfoNV info = {VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV};
        info.srcSize = srcDesc.size;
        info.srcData.hostAddress = (const uint8_t*)srcBuffer + srcDesc.offset;
        info.pDstSize = (size_t*)&dstDesc.size;
        info.dstData.hostAddress = (uint8_t*)dstBuffer + dstDesc.offset;
        info.srcComponentType = translateCooperativeVectorComponentType(srcDesc.componentType);
        info.dstComponentType = translateCooperativeVectorComponentType(dstDesc.componentType);
        info.numRows = srcDesc.rowCount;
        info.numColumns = srcDesc.colCount;
        info.srcLayout = translateCooperativeVectorMatrixLayout(srcDesc.layout);
        info.srcStride = srcDesc.rowColumnStride;
        info.dstLayout = translateCooperativeVectorMatrixLayout(dstDesc.layout);
        info.dstStride = dstDesc.rowColumnStride;
        SLANG_VK_RETURN_ON_FAIL_REPORT(m_api.vkConvertCooperativeVectorMatrixNV(m_api.m_device, &info), this);
    }
    return SLANG_OK;
}

Result DeviceImpl::createInputLayout(const InputLayoutDesc& desc, IInputLayout** outLayout)
{
    RefPtr<InputLayoutImpl> layout(new InputLayoutImpl);

    std::vector<VkVertexInputAttributeDescription>& dstAttributes = layout->m_attributeDescs;
    std::vector<VkVertexInputBindingDescription>& dstStreams = layout->m_streamDescs;

    dstAttributes.resize(desc.inputElementCount);
    dstStreams.resize(desc.vertexStreamCount);

    for (uint32_t i = 0; i < desc.vertexStreamCount; i++)
    {
        const VertexStreamDesc& srcStream = desc.vertexStreams[i];
        VkVertexInputBindingDescription& dstStream = dstStreams[i];
        dstStream.stride = srcStream.stride;
        dstStream.binding = i;
        dstStream.inputRate = (srcStream.slotClass == InputSlotClass::PerInstance) ? VK_VERTEX_INPUT_RATE_INSTANCE
                                                                                   : VK_VERTEX_INPUT_RATE_VERTEX;
    }

    for (uint32_t i = 0; i < desc.inputElementCount; ++i)
    {
        const InputElementDesc& srcDesc = desc.inputElements[i];
        VkVertexInputAttributeDescription& dstDesc = dstAttributes[i];

        dstDesc.location = i;
        dstDesc.binding = srcDesc.bufferSlotIndex;
        dstDesc.format = getVkFormat(srcDesc.format);
        if (dstDesc.format == VK_FORMAT_UNDEFINED)
        {
            return SLANG_FAIL;
        }
        dstDesc.offset = srcDesc.offset;
    }

    // Work out the overall size
    returnComPtr(outLayout, layout);
    return SLANG_OK;
}

Result DeviceImpl::createShaderProgram(
    const ShaderProgramDesc& desc,
    IShaderProgram** outProgram,
    ISlangBlob** outDiagnosticBlob
)
{
    RefPtr<ShaderProgramImpl> shaderProgram = new ShaderProgramImpl(this, desc);
    SLANG_RETURN_ON_FAIL(shaderProgram->init());
    SLANG_RETURN_ON_FAIL(
        RootShaderObjectLayoutImpl::create(
            this,
            shaderProgram->linkedProgram,
            shaderProgram->linkedProgram->getLayout(),
            shaderProgram->getSyntheticResourceBindingState(),
            shaderProgram->m_rootShaderObjectLayout.writeRef()
        )
    );
    returnComPtr(outProgram, shaderProgram);
    return SLANG_OK;
}

Result DeviceImpl::createShaderObjectLayout(
    slang::ISession* session,
    slang::TypeLayoutReflection* typeLayout,
    ShaderObjectLayout** outLayout
)
{
    RefPtr<ShaderObjectLayoutImpl> layout;
    SLANG_RETURN_ON_FAIL(ShaderObjectLayoutImpl::createForElementType(this, session, typeLayout, layout.writeRef()));
    returnRefPtr(outLayout, layout);
    return SLANG_OK;
}

Result DeviceImpl::createRootShaderObjectLayout(
    slang::IComponentType* program,
    slang::ProgramLayout* programLayout,
    ShaderObjectLayout** outLayout
)
{
    return SLANG_FAIL;
}

Result DeviceImpl::createShaderTable(const ShaderTableDesc& desc, IShaderTable** outShaderTable)
{
    RefPtr<ShaderTableImpl> result = new ShaderTableImpl(this, desc);
    returnComPtr(outShaderTable, result);
    return SLANG_OK;
}

Result DeviceImpl::waitForFences(
    uint32_t fenceCount,
    IFence** fences,
    const uint64_t* fenceValues,
    bool waitForAll,
    uint64_t timeout
)
{
    short_vector<VkSemaphore> semaphores;
    for (uint32_t i = 0; i < fenceCount; ++i)
    {
        auto fenceImpl = checked_cast<FenceImpl*>(fences[i]);
        semaphores.push_back(fenceImpl->m_semaphore);
    }
    VkSemaphoreWaitInfo waitInfo;
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.pNext = NULL;
    waitInfo.flags = waitForAll ? 0 : VK_SEMAPHORE_WAIT_ANY_BIT;
    waitInfo.semaphoreCount = fenceCount;
    waitInfo.pSemaphores = semaphores.data();
    waitInfo.pValues = fenceValues;
    auto result = m_api.vkWaitSemaphores(m_api.m_device, &waitInfo, timeout);
    if (result == VK_TIMEOUT)
        return SLANG_E_TIME_OUT;
    if (result != VK_SUCCESS)
    {
        // Report the failure instead of dropping it silently, so that device loss (and any
        // shader-abort message it carries) is surfaced when a caller waits on timeline fences
        // after an aborting dispatch rather than via the command queue.
        reportVulkanError(result, "vkWaitSemaphores", SLANG_RHI_SOURCE_LOCATION(), this);
        return SLANG_FAIL;
    }
    return SLANG_OK;
}

} // namespace rhi::vk
