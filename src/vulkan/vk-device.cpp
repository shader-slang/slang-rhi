#include "vk-device.h"
#include "vk-command.h"
#include "vk-buffer.h"
#include "vk-texture.h"
#include "vk-command.h"
#include "vk-fence.h"
#include "vk-helper-functions.h"
#include "vk-query.h"
#include "vk-sampler.h"
#include "vk-shader-object-layout.h"
#include "vk-shader-object.h"
#include "vk-shader-program.h"
#include "vk-shader-table.h"
#include "vk-input-layout.h"
#include "vk-acceleration-structure.h"

#include "core/common.h"
#include "core/short_vector.h"
#include "core/static_vector.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#ifdef SLANG_RHI_NV_AFTERMATH
#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_Defines.h"
#include "GFSDK_Aftermath_GpuCrashDump.h"
#endif

namespace rhi::vk {

DeviceImpl::DeviceImpl() {}

DeviceImpl::~DeviceImpl()
{
    // Check the device queue is valid else, we can't wait on it..
    if (m_deviceQueue.isValid())
    {
        waitForGpu();
    }

    m_shaderObjectLayoutCache = decltype(m_shaderObjectLayoutCache)();
    m_shaderCache.free();

    if (m_api.vkDestroySampler)
    {
        m_api.vkDestroySampler(m_device, m_defaultSampler, nullptr);
    }

    m_queue.setNull();
    m_deviceQueue.destroy();

    descriptorSetAllocator.close();

    if (m_device != VK_NULL_HANDLE)
    {
        if (!m_desc.existingDeviceHandles.handles[2])
            m_api.vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
        if (m_debugReportCallback != VK_NULL_HANDLE)
            m_api.vkDestroyDebugUtilsMessengerEXT(m_api.m_instance, m_debugReportCallback, nullptr);
        if (m_api.m_instance != VK_NULL_HANDLE && !m_desc.existingDeviceHandles.handles[0])
            m_api.vkDestroyInstance(m_api.m_instance, nullptr);
    }
}

VkBool32 DeviceImpl::handleDebugMessage(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData
)
{
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

template<typename T>
static bool _hasAnySetBits(const T& val, size_t offset)
{
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&val);
    for (size_t i = offset; i < sizeof(val); i++)
        if (ptr[i])
            return true;
    return false;
}

Result DeviceImpl::initVulkanInstanceAndDevice(
    const NativeHandle* handles,
    bool enableValidationLayer,
    bool enableRayTracingValidation
)
{
    m_features.clear();

    VkInstance instance = VK_NULL_HANDLE;
    if (!handles[0])
    {
        VkApplicationInfo applicationInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
        applicationInfo.pApplicationName = "slang-rhi";
        applicationInfo.pEngineName = "slang-rhi";
        applicationInfo.apiVersion = VK_API_VERSION_1_1;
        applicationInfo.engineVersion = 1;
        applicationInfo.applicationVersion = 1;

        static_vector<const char*, 16> instanceExtensions;

#if SLANG_APPLE_FAMILY
        instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif
        instanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        instanceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);

        // Software (swiftshader) implementation currently does not support surface extension,
        // so only use it with a hardware implementation.
        if (!m_api.m_module->isSoftware())
        {
            instanceExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
            // Note: this extension is not yet supported by nvidia drivers, disable for now.
            // instanceExtensions.push_back("VK_GOOGLE_surfaceless_query");
#if SLANG_WINDOWS_FAMILY
            instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif SLANG_APPLE_FAMILY
            instanceExtensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#elif SLANG_LINUX_FAMILY
            instanceExtensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#endif
        }

        if (enableValidationLayer)
        {
            instanceExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
            instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
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
        VkValidationFeatureEnableEXT enabledValidationFeatures[1] = {VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT};
        if (enableValidationLayer)
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

                // Include support for printf
                validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
                validationFeatures.enabledValidationFeatureCount = 1;
                validationFeatures.pEnabledValidationFeatures = enabledValidationFeatures;
                instanceCreateInfo.pNext = &validationFeatures;
            }
        }
        uint32_t apiVersionsToTry[] = {VK_API_VERSION_1_3, VK_API_VERSION_1_2, VK_API_VERSION_1_1, VK_API_VERSION_1_0};
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
                break;
            }
        }
    }
    else
    {
        if (handles[0].type != NativeHandleType::VkInstance)
        {
            return SLANG_FAIL;
        }
        instance = (VkInstance)handles[0].value;
    }
    if (!instance)
        return SLANG_FAIL;
    SLANG_RETURN_ON_FAIL(m_api.initInstanceProcs(instance));

    if ((enableRayTracingValidation || enableValidationLayer) && m_api.vkCreateDebugUtilsMessengerEXT)
    {
        VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo = {
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT
        };
        messengerCreateInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        messengerCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messengerCreateInfo.pfnUserCallback = &debugMessageCallback;
        messengerCreateInfo.pUserData = this;

        SLANG_VK_RETURN_ON_FAIL(
            m_api.vkCreateDebugUtilsMessengerEXT(instance, &messengerCreateInfo, nullptr, &m_debugReportCallback)
        );
    }

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    if (!handles[1])
    {
        uint32_t numPhysicalDevices = 0;
        SLANG_VK_RETURN_ON_FAIL(m_api.vkEnumeratePhysicalDevices(instance, &numPhysicalDevices, nullptr));

        std::vector<VkPhysicalDevice> physicalDevices;
        physicalDevices.resize(numPhysicalDevices);
        SLANG_VK_RETURN_ON_FAIL(m_api.vkEnumeratePhysicalDevices(instance, &numPhysicalDevices, physicalDevices.data())
        );

        // Use first physical device by default.
        int selectedDeviceIndex = -1;

        // Search for requested adapter.
        if (m_desc.adapterLUID)
        {
            for (size_t i = 0; i < physicalDevices.size(); ++i)
            {
                if (vk::getAdapterLUID(m_api, physicalDevices[i]) == *m_desc.adapterLUID)
                {
                    selectedDeviceIndex = (int)i;
                    break;
                }
            }
            if (selectedDeviceIndex < 0)
                return SLANG_E_NOT_FOUND;
        }

        if (selectedDeviceIndex == -1)
        {
            // If no device is explicitly selected by the user,
            // we will select the device with most amount of extensions.
            selectedDeviceIndex = 0;
            uint32_t currentMaxExtensionCount = 0;
            for (size_t i = 0; i < physicalDevices.size(); ++i)
            {
                uint32_t propCount = 0;
                m_api.vkEnumerateDeviceExtensionProperties(physicalDevices[i], NULL, &propCount, nullptr);
                if (propCount > currentMaxExtensionCount)
                {
                    selectedDeviceIndex = (int)i;
                    currentMaxExtensionCount = propCount;
                }
            }
        }

        if (selectedDeviceIndex >= physicalDevices.size())
            return SLANG_FAIL;

        physicalDevice = physicalDevices[selectedDeviceIndex];
    }
    else
    {
        if (handles[1].type != NativeHandleType::VkPhysicalDevice)
        {
            return SLANG_FAIL;
        }
        physicalDevice = (VkPhysicalDevice)handles[1].value;
    }

    SLANG_RETURN_ON_FAIL(m_api.initPhysicalDevice(physicalDevice));

    // Obtain the name of the selected adapter.
    {
        VkPhysicalDeviceProperties basicProps = {};
        m_api.vkGetPhysicalDeviceProperties(physicalDevice, &basicProps);
        m_adapterName = basicProps.deviceName;
        m_info.adapterName = m_adapterName.data();
    }

    // Query the available extensions
    uint32_t extensionCount = 0;
    m_api.vkEnumerateDeviceExtensionProperties(m_api.m_physicalDevice, NULL, &extensionCount, NULL);
    std::vector<VkExtensionProperties> extensions;
    extensions.resize(extensionCount);
    m_api.vkEnumerateDeviceExtensionProperties(m_api.m_physicalDevice, NULL, &extensionCount, extensions.data());
    std::set<std::string> extensionNames;
    for (const auto& e : extensions)
        extensionNames.emplace(e.extensionName);

    std::vector<const char*> deviceExtensions;
    deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
#if SLANG_APPLE_FAMILY
    deviceExtensions.push_back("VK_KHR_portability_subset");
#endif

    VkDeviceCreateInfo deviceCreateInfo = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pEnabledFeatures = &m_api.m_deviceFeatures;

    // Get the device features (doesn't use, but useful when debugging)
    if (m_api.vkGetPhysicalDeviceFeatures2)
    {
        VkPhysicalDeviceFeatures2 deviceFeatures2 = {};
        deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        m_api.vkGetPhysicalDeviceFeatures2(m_api.m_physicalDevice, &deviceFeatures2);
    }

    VkPhysicalDeviceProperties basicProps = {};
    m_api.vkGetPhysicalDeviceProperties(m_api.m_physicalDevice, &basicProps);

    // Compute timestamp frequency.
    m_info.timestampFrequency = uint64_t(1e9 / basicProps.limits.timestampPeriod);

    // Get device limits.
    {
        DeviceLimits limits = {};
        limits.maxTextureDimension1D = basicProps.limits.maxImageDimension1D;
        limits.maxTextureDimension2D = basicProps.limits.maxImageDimension2D;
        limits.maxTextureDimension3D = basicProps.limits.maxImageDimension3D;
        limits.maxTextureDimensionCube = basicProps.limits.maxImageDimensionCube;
        limits.maxTextureArrayLayers = basicProps.limits.maxImageArrayLayers;

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

        m_info.limits = limits;
    }

    // Get the API version
    const uint32_t majorVersion = VK_VERSION_MAJOR(basicProps.apiVersion);
    const uint32_t minorVersion = VK_VERSION_MINOR(basicProps.apiVersion);

    auto& extendedFeatures = m_api.m_extendedFeatures;

    // API version check, can't use vkGetPhysicalDeviceProperties2 yet since this device might not
    // support it
    if (VK_MAKE_VERSION(majorVersion, minorVersion, 0) >= VK_API_VERSION_1_1 && m_api.vkGetPhysicalDeviceProperties2 &&
        m_api.vkGetPhysicalDeviceFeatures2)
    {
        // Get device features
        VkPhysicalDeviceFeatures2 deviceFeatures2 = {};
        deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

        // Inline uniform block
        extendedFeatures.inlineUniformBlockFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.inlineUniformBlockFeatures;

        // Ray query features
        extendedFeatures.rayQueryFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.rayQueryFeatures;

        // Ray tracing pipeline features
        extendedFeatures.rayTracingPipelineFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.rayTracingPipelineFeatures;

        // SER features.
        extendedFeatures.rayTracingInvocationReorderFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.rayTracingInvocationReorderFeatures;

        // Acceleration structure features
        extendedFeatures.accelerationStructureFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.accelerationStructureFeatures;

        // Variable pointer features.
        extendedFeatures.variablePointersFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.variablePointersFeatures;

        // Compute shader derivative features.
        extendedFeatures.computeShaderDerivativeFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.computeShaderDerivativeFeatures;

        // Extended dynamic states
        extendedFeatures.extendedDynamicStateFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.extendedDynamicStateFeatures;

        // 16-bit storage
        extendedFeatures.storage16BitFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.storage16BitFeatures;

        // robustness2 features
        extendedFeatures.robustness2Features.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.robustness2Features;

        // clock features
        extendedFeatures.clockFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.clockFeatures;

        // Atomic Float
        // To detect atomic float we need
        // https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkPhysicalDeviceShaderAtomicFloatFeaturesEXT.html

        extendedFeatures.atomicFloatFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.atomicFloatFeatures;

        // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT.html
        extendedFeatures.atomicFloat2Features.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.atomicFloat2Features;

        // Image Int64 Atomic
        // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT.html
        extendedFeatures.imageInt64AtomicFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.imageInt64AtomicFeatures;

        // mesh shader features
        extendedFeatures.meshShaderFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.meshShaderFeatures;

        // multiview features
        extendedFeatures.multiviewFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.multiviewFeatures;

        // fragment shading rate features
        extendedFeatures.fragmentShadingRateFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.fragmentShadingRateFeatures;

        // raytracing validation features
        extendedFeatures.rayTracingValidationFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.rayTracingValidationFeatures;

        // dynamic rendering features
        extendedFeatures.dynamicRenderingFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.dynamicRenderingFeatures;

        extendedFeatures.dynamicRenderingLocalReadFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.dynamicRenderingLocalReadFeatures;

        extendedFeatures.formats4444Features.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.formats4444Features;

        extendedFeatures.shaderMaximalReconvergenceFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.shaderMaximalReconvergenceFeatures;

        extendedFeatures.shaderQuadControlFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.shaderQuadControlFeatures;

        extendedFeatures.shaderIntegerDotProductFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.shaderIntegerDotProductFeatures;

        extendedFeatures.cooperativeVectorFeatures.pNext = deviceFeatures2.pNext;
        deviceFeatures2.pNext = &extendedFeatures.cooperativeVectorFeatures;

        if (VK_MAKE_VERSION(majorVersion, minorVersion, 0) >= VK_API_VERSION_1_2)
        {
            extendedFeatures.vulkan12Features.pNext = deviceFeatures2.pNext;
            deviceFeatures2.pNext = &extendedFeatures.vulkan12Features;
        }

        // if (VK_MAKE_VERSION(majorVersion, minorVersion, 0) >= VK_API_VERSION_1_3)
        // {
        //     extendedFeatures.vulkan13Features.pNext = deviceFeatures2.pNext;
        //     deviceFeatures2.pNext = &extendedFeatures.vulkan13Features;
        // }

        m_api.vkGetPhysicalDeviceFeatures2(m_api.m_physicalDevice, &deviceFeatures2);

        if (deviceFeatures2.features.shaderResourceMinLod)
        {
            m_features.push_back("shader-resource-min-lod");
        }
        if (deviceFeatures2.features.shaderFloat64)
        {
            m_features.push_back("double");
        }
        if (deviceFeatures2.features.shaderInt64)
        {
            m_features.push_back("int64");
        }
        if (deviceFeatures2.features.shaderInt16)
        {
            m_features.push_back("int16");
        }
        // If we have float16 features then enable
        if (extendedFeatures.vulkan12Features.shaderFloat16)
        {
            // We have half support
            m_features.push_back("half");
        }

        const auto addFeatureExtension = [&](const bool feature, auto& featureStruct, const char* extension = nullptr)
        {
            if (!feature)
                return false;
            if (extension)
            {
                if (!extensionNames.count(extension))
                    return false;
                deviceExtensions.push_back(extension);
            }
            featureStruct.pNext = (void*)deviceCreateInfo.pNext;
            deviceCreateInfo.pNext = &featureStruct;
            return true;
        };

        // SIMPLE_EXTENSION_FEATURE(struct, feature member name, extension
        // name, features...) will check for the presence of the boolean
        // feature member in struct and the availability of the extensions. If
        // they are both present then the extensions are added, the struct
        // linked into the deviceCreateInfo chain and the features added to the
        // supported features list.
#define SIMPLE_EXTENSION_FEATURE(s, m, e, ...)                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        const static auto fs = {__VA_ARGS__};                                                                          \
        if (addFeatureExtension(s.m, s, e))                                                                            \
            for (const auto& p : fs)                                                                                   \
                m_features.push_back(p);                                                                               \
    }                                                                                                                  \
    while (0)

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.dynamicRenderingFeatures,
            dynamicRendering,
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
            "dynamic-rendering"
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.dynamicRenderingLocalReadFeatures,
            dynamicRenderingLocalRead,
            VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME,
            "dynamic-rendering"
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.formats4444Features,
            formatA4R4G4B4,
            VK_EXT_4444_FORMATS_EXTENSION_NAME,
            "b4g4r4a4-format"
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.storage16BitFeatures,
            storageBuffer16BitAccess,
            VK_KHR_16BIT_STORAGE_EXTENSION_NAME,
            "16-bit-storage"
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.atomicFloatFeatures,
            shaderBufferFloat32Atomics,
            VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME,
            "atomic-float"
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.atomicFloat2Features,
            shaderBufferFloat16Atomics,
            VK_EXT_SHADER_ATOMIC_FLOAT_2_EXTENSION_NAME,
            "atomic-float-2"
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.imageInt64AtomicFeatures,
            shaderImageInt64Atomics,
            VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME,
            "image-atomic-int64"
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.extendedDynamicStateFeatures,
            extendedDynamicState,
            VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
            "extended-dynamic-states"
        );

        if (extendedFeatures.accelerationStructureFeatures.accelerationStructure &&
            extensionNames.count(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
            extensionNames.count(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME))
        {
            extendedFeatures.accelerationStructureFeatures.pNext = (void*)deviceCreateInfo.pNext;
            deviceCreateInfo.pNext = &extendedFeatures.accelerationStructureFeatures;
            deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            m_features.push_back("acceleration-structure");

            // These both depend on VK_KHR_acceleration_structure

            SIMPLE_EXTENSION_FEATURE(
                extendedFeatures.rayQueryFeatures,
                rayQuery,
                VK_KHR_RAY_QUERY_EXTENSION_NAME,
                "ray-query",
                "ray-tracing"
            );

            SIMPLE_EXTENSION_FEATURE(
                extendedFeatures.rayTracingPipelineFeatures,
                rayTracingPipeline,
                VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
                "ray-tracing-pipeline"
            );
        }

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.inlineUniformBlockFeatures,
            inlineUniformBlock,
            VK_EXT_INLINE_UNIFORM_BLOCK_EXTENSION_NAME,
            "inline-uniform-block",
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.robustness2Features,
            nullDescriptor,
            VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
            "robustness2",
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.clockFeatures,
            shaderDeviceClock,
            VK_KHR_SHADER_CLOCK_EXTENSION_NAME,
            "realtime-clock"
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.meshShaderFeatures,
            meshShader,
            VK_EXT_MESH_SHADER_EXTENSION_NAME,
            "mesh-shader"
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.multiviewFeatures,
            multiview,
            VK_KHR_MULTIVIEW_EXTENSION_NAME,
            "multiview"
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.fragmentShadingRateFeatures,
            primitiveFragmentShadingRate,
            VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME,
            "fragment-shading-rate"
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.rayTracingInvocationReorderFeatures,
            rayTracingInvocationReorder,
            VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME,
            "shader-execution-reorder"
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.variablePointersFeatures,
            variablePointers,
            VK_KHR_VARIABLE_POINTERS_EXTENSION_NAME,
            "variable-pointer"
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.computeShaderDerivativeFeatures,
            computeDerivativeGroupLinear,
            VK_NV_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME,
            "computeDerivativeGroupLinear"
        );

        // Only enable raytracing validation if both requested and supported
        if (enableRayTracingValidation && extendedFeatures.rayTracingValidationFeatures.rayTracingValidation)
        {
            SIMPLE_EXTENSION_FEATURE(
                extendedFeatures.rayTracingValidationFeatures,
                rayTracingValidation,
                VK_NV_RAY_TRACING_VALIDATION_EXTENSION_NAME,
                "ray-tracing-validation"
            );
        }

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.shaderMaximalReconvergenceFeatures,
            shaderMaximalReconvergence,
            VK_KHR_SHADER_MAXIMAL_RECONVERGENCE_EXTENSION_NAME,
            "shader-maximal-reconvergence"
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.shaderQuadControlFeatures,
            shaderQuadControl,
            VK_KHR_SHADER_QUAD_CONTROL_EXTENSION_NAME,
            "shader-quad-control"
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.shaderIntegerDotProductFeatures,
            shaderIntegerDotProduct,
            VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME,
            "integer-dot-product"
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.cooperativeVectorFeatures,
            cooperativeVector,
            VK_NV_COOPERATIVE_VECTOR_EXTENSION_NAME,
            "cooperative-vector"
        );

#undef SIMPLE_EXTENSION_FEATURE

        if (extendedFeatures.vulkan12Features.shaderBufferInt64Atomics)
            m_features.push_back("atomic-int64");

        if (extendedFeatures.vulkan12Features.timelineSemaphore)
            m_features.push_back("timeline-semaphore");

        if (extendedFeatures.vulkan12Features.shaderSubgroupExtendedTypes)
            m_features.push_back("shader-subgroup-extended-types");

        if (extendedFeatures.vulkan12Features.bufferDeviceAddress)
            m_features.push_back("buffer-device-address");

        if (_hasAnySetBits(
                extendedFeatures.vulkan12Features,
                offsetof(VkPhysicalDeviceVulkan12Features, pNext) + sizeof(void*)
            ))
        {
            extendedFeatures.vulkan12Features.pNext = (void*)deviceCreateInfo.pNext;
            deviceCreateInfo.pNext = &extendedFeatures.vulkan12Features;
        }

        VkPhysicalDeviceProperties2 extendedProps = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR
        };
        VkPhysicalDeviceSubgroupProperties subgroupProps = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};

        rtProps.pNext = extendedProps.pNext;
        extendedProps.pNext = &rtProps;
        subgroupProps.pNext = extendedProps.pNext;
        extendedProps.pNext = &subgroupProps;

        m_api.vkGetPhysicalDeviceProperties2(m_api.m_physicalDevice, &extendedProps);
        m_api.m_rtProperties = rtProps;

        // Approximate DX12's WaveOps boolean
        if (subgroupProps.supportedOperations &
            (VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT | VK_SUBGROUP_FEATURE_ARITHMETIC_BIT |
             VK_SUBGROUP_FEATURE_BALLOT_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
             VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT | VK_SUBGROUP_FEATURE_CLUSTERED_BIT |
             VK_SUBGROUP_FEATURE_QUAD_BIT | VK_SUBGROUP_FEATURE_PARTITIONED_BIT_NV))
        {
            m_features.push_back("wave-ops");
        }

        if (extensionNames.count("VK_KHR_external_memory"))
        {
            deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
#if SLANG_WINDOWS_FAMILY
            if (extensionNames.count("VK_KHR_external_memory_win32"))
            {
                deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
            }
#else
            if (extensionNames.count("VK_KHR_external_memory_fd"))
            {
                deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
            }
#endif
            m_features.push_back("external-memory");
        }
        if (extensionNames.count(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
#if SLANG_WINDOWS_FAMILY
            if (extensionNames.count(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME))
            {
                deviceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
            }
#else
            if (extensionNames.count(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME))
            {
                deviceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
            }
#endif
            m_features.push_back("external-semaphore");
        }
        if (extensionNames.count(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);
            m_features.push_back("conservative-rasterization-3");
            m_features.push_back("conservative-rasterization-2");
            m_features.push_back("conservative-rasterization-1");
        }
        if (extensionNames.count(VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME);
        }
        if (extensionNames.count(VK_NVX_BINARY_IMPORT_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_NVX_BINARY_IMPORT_EXTENSION_NAME);
            m_features.push_back("nvx-binary-import");
        }
        if (extensionNames.count(VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME);
            m_features.push_back("nvx-image-view-handle");
        }
        if (extensionNames.count(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
            m_features.push_back("push-descriptor");
        }
        if (extensionNames.count(VK_NV_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_NV_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME);
            m_features.push_back("barycentrics");
        }
        if (extensionNames.count(VK_NV_SHADER_SUBGROUP_PARTITIONED_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_NV_SHADER_SUBGROUP_PARTITIONED_EXTENSION_NAME);
            m_features.push_back("shader-subgroup-partitioned");
        }

        // Derive approximate DX12 shader model.
        const char* featureTable[] = {
            "sm_6_0",
            "wave-ops",
            "atomic-int64",
            nullptr,
            "sm_6_1",
            "barycentrics",
            "multiview",
            nullptr,
            "sm_6_2",
            "half",
            nullptr,
            "sm_6_3",
            "ray-tracing-pipeline",
            nullptr,
            "sm_6_4",
            "fragment-shading-rate",
            nullptr,
            "sm_6_5",
            "ray-query",
            "mesh-shader",
            nullptr,
            "sm_6_6",
            "wave-ops",
            "atomic-float",
            "atomic-int64",
            nullptr,
            nullptr,
        };

        int i = 0;
        while (i < SLANG_COUNT_OF(featureTable))
        {
            const char* sm = featureTable[i++];
            if (sm == nullptr)
            {
                break;
            }
            bool hasAll = true;
            while (i < SLANG_COUNT_OF(featureTable))
            {
                const char* feature = featureTable[i++];
                if (feature == nullptr)
                {
                    break;
                }
                hasAll &= std::any_of(
                    m_features.begin(),
                    m_features.end(),
                    [&](const std::string& f) { return f == feature; }
                );
            }
            if (hasAll)
            {
                m_features.push_back(sm);
            }
            else
            {
                break;
            }
        }
    }

    // Supports ParameterBlock
    m_features.push_back("parameter-block");
    // Supports surface/swapchain
    m_features.push_back("surface");
    // Supports rasterization
    m_features.push_back("rasterization");

    if (m_api.m_module->isSoftware())
    {
        m_features.push_back("software-device");
    }
    else
    {
        m_features.push_back("hardware-device");
    }

    int queueFamilyIndex = m_api.findQueue(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
    SLANG_RHI_ASSERT(queueFamilyIndex >= 0);
    m_queueFamilyIndex = queueFamilyIndex;

#if defined(SLANG_RHI_NV_AFTERMATH)
    VkDeviceDiagnosticsConfigCreateInfoNV aftermathInfo = {};

    {
        // Enable NV_device_diagnostic_checkpoints extension to be able to
        // use Aftermath event markers.
        deviceExtensions.push_back(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);

        // Enable NV_device_diagnostics_config extension to configure Aftermath
        // features.
        deviceExtensions.push_back(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);

        // Set up device creation info for Aftermath feature flag configuration.
        VkDeviceDiagnosticsConfigFlagsNV aftermathFlags =
            VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_AUTOMATIC_CHECKPOINTS_BIT_NV | // Enable automatic call stack
                                                                               // checkpoints.
            VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV |     // Enable tracking of resources.
            VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV; // Generate debug information for shaders.
        // Not available on the version of Vulkan currently building with.
        // VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_ERROR_REPORTING_BIT_NV;  // Enable additional runtime shader error
        // reporting.

        aftermathInfo.sType = VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV;
        aftermathInfo.flags = aftermathFlags;

        aftermathInfo.pNext = deviceCreateInfo.pNext;
        deviceCreateInfo.pNext = &aftermathInfo;
    }
#endif

    if (!handles[2])
    {
        float queuePriority = 0.0f;
        VkDeviceQueueCreateInfo queueCreateInfo = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueCreateInfo.queueFamilyIndex = m_queueFamilyIndex;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;

        deviceCreateInfo.enabledExtensionCount = uint32_t(deviceExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

        if (m_api.vkCreateDevice(m_api.m_physicalDevice, &deviceCreateInfo, nullptr, &m_device) != VK_SUCCESS)
            return SLANG_FAIL;
    }
    else
    {
        if (handles[2].type != NativeHandleType::VkDevice)
        {
            return SLANG_FAIL;
        }
        m_device = (VkDevice)handles[2].value;
    }

    SLANG_RETURN_ON_FAIL(m_api.initDeviceProcs(m_device));

    return SLANG_OK;
}

Result DeviceImpl::initialize(const DeviceDesc& desc)
{
    // Initialize device info.
    {
        m_info.apiName = "Vulkan";
        m_info.deviceType = DeviceType::Vulkan;
        static const float kIdentity[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        ::memcpy(m_info.identityProjectionMatrix, kIdentity, sizeof(kIdentity));
    }

    m_desc = desc;

    SLANG_RETURN_ON_FAIL(Device::initialize(desc));
    Result initDeviceResult = SLANG_OK;

    for (int forceSoftware = 0; forceSoftware <= 1; forceSoftware++)
    {
        initDeviceResult = m_module.init(forceSoftware != 0);
        if (initDeviceResult != SLANG_OK)
            continue;
        initDeviceResult = m_api.initGlobalProcs(m_module);
        if (initDeviceResult != SLANG_OK)
            continue;
        descriptorSetAllocator.m_api = &m_api;
        initDeviceResult = initVulkanInstanceAndDevice(
            desc.existingDeviceHandles.handles,
            isDebugLayersEnabled(),
            desc.enableRayTracingValidation
        );
        if (initDeviceResult == SLANG_OK)
            break;
    }
    SLANG_RETURN_ON_FAIL(initDeviceResult);

    {
        VkQueue queue;
        m_api.vkGetDeviceQueue(m_device, m_queueFamilyIndex, 0, &queue);
        SLANG_RETURN_ON_FAIL(m_deviceQueue.init(m_api, queue, m_queueFamilyIndex));
    }

    SLANG_RETURN_ON_FAIL(
        m_slangContext
            .initialize(desc.slang, SLANG_SPIRV, "sm_6_0", std::array{slang::PreprocessorMacroDesc{"__VK__", "1"}})
    );

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
        SLANG_VK_RETURN_ON_FAIL(m_api.vkCreateSampler(m_device, &samplerInfo, nullptr, &m_defaultSampler));
    }

    m_queue = new CommandQueueImpl(this, QueueType::Graphics);
    m_queue->init(m_deviceQueue.getQueue(), m_queueFamilyIndex);

    return SLANG_OK;
}

void DeviceImpl::waitForGpu()
{
    m_deviceQueue.flushAndWait();
}

const DeviceInfo& DeviceImpl::getDeviceInfo() const
{
    return m_info;
}

Result DeviceImpl::getQueue(QueueType type, ICommandQueue** outQueue)
{
    if (type != QueueType::Graphics)
        return SLANG_FAIL;
    m_queue->establishStrongReferenceToDevice();
    returnComPtr(outQueue, m_queue);
    return SLANG_OK;
}

Result DeviceImpl::readTexture(ITexture* texture, ISlangBlob** outBlob, Size* outRowPitch, Size* outPixelSize)
{
    TextureImpl* textureImpl = checked_cast<TextureImpl*>(texture);

    const TextureDesc& desc = textureImpl->m_desc;
    uint32_t width = desc.size.width;
    const FormatInfo& formatInfo = getFormatInfo(desc.format);
    Size pixelSize = formatInfo.blockSizeInBytes / formatInfo.pixelsPerBlock;
    Size rowPitch = width * pixelSize;
    uint32_t arrayLayerCount = desc.arrayLength * (desc.type == TextureType::TextureCube ? 6 : 1);

    std::vector<Extents> mipSizes;

    // Calculate how large the buffer has to be
    Size bufferSize = 0;
    // Calculate how large an array entry is
    for (uint32_t j = 0; j < desc.mipLevelCount; ++j)
    {
        const Extents mipSize = calcMipSize(desc.size, j);

        auto rowSizeInBytes = calcRowSize(desc.format, mipSize.width);
        auto numRows = calcNumRows(desc.format, mipSize.height);

        mipSizes.push_back(mipSize);

        bufferSize += (rowSizeInBytes * numRows) * mipSize.depth;
    }
    // Calculate the total size taking into account the array
    bufferSize *= arrayLayerCount;

    VKBufferHandleRAII staging;
    SLANG_RETURN_ON_FAIL(staging.init(
        m_api,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    ));

    VkCommandBuffer commandBuffer = m_deviceQueue.getCommandBuffer();
    VkImage srcImage = textureImpl->m_image;

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = srcImage;
    barrier.oldLayout = translateImageLayout(textureImpl->m_desc.defaultState);
    barrier.newLayout = translateImageLayout(ResourceState::CopySource);
    barrier.subresourceRange.aspectMask = getAspectMaskFromFormat(VulkanUtil::getVkFormat(textureImpl->m_desc.format));
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.srcAccessMask = calcAccessFlags(textureImpl->m_desc.defaultState);
    barrier.dstAccessMask = calcAccessFlags(ResourceState::CopySource);

    VkPipelineStageFlags srcStageFlags = calcPipelineStageFlags(textureImpl->m_desc.defaultState, true);
    VkPipelineStageFlags dstStageFlags = calcPipelineStageFlags(ResourceState::CopySource, false);

    m_api.vkCmdPipelineBarrier(
        commandBuffer,
        srcStageFlags,
        dstStageFlags,
        VkDependencyFlags(0),
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );

    VkImageLayout srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    uint64_t dstOffset = 0;
    for (uint32_t i = 0; i < arrayLayerCount; ++i)
    {
        for (size_t j = 0; j < mipSizes.size(); ++j)
        {
            const auto& mipSize = mipSizes[j];

            auto rowSizeInBytes = calcRowSize(desc.format, mipSize.width);
            auto numRows = calcNumRows(desc.format, mipSize.height);

            VkBufferImageCopy region = {};

            region.bufferOffset = dstOffset;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;

            region.imageSubresource.aspectMask = getAspectMaskFromFormat(VulkanUtil::getVkFormat(desc.format));
            region.imageSubresource.mipLevel = uint32_t(j);
            region.imageSubresource.baseArrayLayer = i;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {uint32_t(mipSize.width), uint32_t(mipSize.height), uint32_t(mipSize.depth)};

            m_api.vkCmdCopyImageToBuffer(commandBuffer, srcImage, srcImageLayout, staging.m_buffer, 1, &region);

            dstOffset += rowSizeInBytes * numRows * mipSize.depth;
        }
    }

    std::swap(barrier.oldLayout, barrier.newLayout);
    std::swap(barrier.srcAccessMask, barrier.dstAccessMask);
    std::swap(srcStageFlags, dstStageFlags);

    m_api.vkCmdPipelineBarrier(
        commandBuffer,
        srcStageFlags,
        dstStageFlags,
        VkDependencyFlags(0),
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );

    m_deviceQueue.flushAndWait();

    auto blob = OwnedBlob::create(bufferSize);

    // Write out the data from the buffer
    void* mappedData = nullptr;
    SLANG_RETURN_ON_FAIL(m_api.vkMapMemory(m_device, staging.m_memory, 0, bufferSize, 0, &mappedData));

    ::memcpy((void*)blob->getBufferPointer(), mappedData, bufferSize);
    m_api.vkUnmapMemory(m_device, staging.m_memory);

    *outPixelSize = pixelSize;
    *outRowPitch = rowPitch;

    returnComPtr(outBlob, blob);
    return SLANG_OK;
}

Result DeviceImpl::readBuffer(IBuffer* inBuffer, Offset offset, Size size, ISlangBlob** outBlob)
{
    BufferImpl* buffer = checked_cast<BufferImpl*>(inBuffer);

    // create staging buffer
    VKBufferHandleRAII staging;

    SLANG_RETURN_ON_FAIL(staging.init(
        m_api,
        size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    ));

    // Copy from real buffer to staging buffer
    VkCommandBuffer commandBuffer = m_deviceQueue.getCommandBuffer();

    VkBufferMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = calcAccessFlags(buffer->m_desc.defaultState);
    barrier.dstAccessMask = calcAccessFlags(ResourceState::CopyDestination);
    barrier.buffer = buffer->m_buffer.m_buffer;
    barrier.offset = 0;
    barrier.size = buffer->m_desc.size;

    VkPipelineStageFlags srcStageFlags = calcPipelineStageFlags(buffer->m_desc.defaultState, true);
    VkPipelineStageFlags dstStageFlags = calcPipelineStageFlags(ResourceState::CopySource, false);

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
    m_api.vkCmdCopyBuffer(commandBuffer, buffer->m_buffer.m_buffer, staging.m_buffer, 1, &copyInfo);

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

    auto blob = OwnedBlob::create(size);

    // Write out the data from the buffer
    void* mappedData = nullptr;
    SLANG_RETURN_ON_FAIL(m_api.vkMapMemory(m_device, staging.m_memory, 0, size, 0, &mappedData));

    ::memcpy((void*)blob->getBufferPointer(), mappedData, size);
    m_api.vkUnmapMemory(m_device, staging.m_memory);

    returnComPtr(outBlob, blob);
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
    AccelerationStructureBuildGeometryInfoBuilder geomInfoBuilder;
    SLANG_RETURN_ON_FAIL(geomInfoBuilder.build(desc, m_debugCallback));
    m_api.vkGetAccelerationStructureBuildSizesKHR(
        m_api.m_device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &geomInfoBuilder.buildInfo,
        geomInfoBuilder.primitiveCounts.data(),
        &sizeInfo
    );
    outSizes->accelerationStructureSize = sizeInfo.accelerationStructureSize;
    outSizes->scratchSize = sizeInfo.buildScratchSize;
    outSizes->updateScratchSize = sizeInfo.updateScratchSize;
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
    bufferDesc.defaultState = ResourceState::AccelerationStructure;
    SLANG_RETURN_ON_FAIL(createBuffer(bufferDesc, nullptr, (IBuffer**)result->m_buffer.writeRef()));
    result->m_device = this;
    VkAccelerationStructureCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    createInfo.buffer = result->m_buffer->m_buffer.m_buffer;
    createInfo.offset = 0;
    createInfo.size = desc.size;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR;
    SLANG_VK_RETURN_ON_FAIL(
        m_api.vkCreateAccelerationStructureKHR(m_api.m_device, &createInfo, nullptr, &result->m_vkHandle)
    );
    returnComPtr(outAccelerationStructure, result);
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
    barrier.subresourceRange.levelCount = desc.mipLevelCount;
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

Result DeviceImpl::getTextureAllocationInfo(const TextureDesc& descIn, Size* outSize, Size* outAlignment)
{
    TextureDesc desc = fixupTextureDesc(descIn);

    const VkFormat format = VulkanUtil::getVkFormat(desc.format);
    if (format == VK_FORMAT_UNDEFINED)
    {
        SLANG_RHI_ASSERT_FAILURE("Unhandled image format");
        return SLANG_FAIL;
    }
    VkImageCreateInfo imageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    switch (desc.type)
    {
    case TextureType::Texture1D:
    {
        imageInfo.imageType = VK_IMAGE_TYPE_1D;
        imageInfo.extent = VkExtent3D{uint32_t(descIn.size.width), 1, 1};
        break;
    }
    case TextureType::Texture2D:
    {
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = VkExtent3D{uint32_t(descIn.size.width), uint32_t(descIn.size.height), 1};
        break;
    }
    case TextureType::TextureCube:
    {
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = VkExtent3D{uint32_t(descIn.size.width), uint32_t(descIn.size.height), 1};
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        break;
    }
    case TextureType::Texture3D:
    {
        // Can't have an array and 3d texture
        SLANG_RHI_ASSERT(desc.arrayLength <= 1);
        imageInfo.imageType = VK_IMAGE_TYPE_3D;
        imageInfo.extent =
            VkExtent3D{uint32_t(descIn.size.width), uint32_t(descIn.size.height), uint32_t(descIn.size.depth)};
        break;
    }
    default:
    {
        SLANG_RHI_ASSERT_FAILURE("Unhandled type");
        return SLANG_FAIL;
    }
    }

    imageInfo.mipLevels = desc.mipLevelCount;
    imageInfo.arrayLayers = desc.arrayLength * (desc.type == TextureType::TextureCube ? 6 : 1);

    imageInfo.format = format;

    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = _calcImageUsageFlags(desc.usage, desc.memoryType, nullptr);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    imageInfo.samples = (VkSampleCountFlagBits)desc.sampleCount;

    VkImage image;
    SLANG_VK_RETURN_ON_FAIL(m_api.vkCreateImage(m_device, &imageInfo, nullptr, &image));

    VkMemoryRequirements memRequirements;
    m_api.vkGetImageMemoryRequirements(m_device, image, &memRequirements);

    *outSize = (Size)memRequirements.size;
    *outAlignment = (Size)memRequirements.alignment;

    m_api.vkDestroyImage(m_device, image, nullptr);
    return SLANG_OK;
}

Result DeviceImpl::getTextureRowAlignment(Size* outAlignment)
{
    *outAlignment = 1;
    return SLANG_OK;
}

Result DeviceImpl::getCooperativeVectorProperties(CooperativeVectorProperties* properties, uint32_t* propertyCount)
{
    if (!m_api.m_extendedFeatures.cooperativeVectorFeatures.cooperativeVector ||
        !m_api.vkGetPhysicalDeviceCooperativeVectorPropertiesNV)
        return SLANG_E_NOT_AVAILABLE;

    if (m_cooperativeVectorProperties.empty())
    {
        uint32_t vkPropertyCount = 0;
        m_api.vkGetPhysicalDeviceCooperativeVectorPropertiesNV(m_api.m_physicalDevice, &vkPropertyCount, nullptr);
        std::vector<VkCooperativeVectorPropertiesNV> vkProperties(vkPropertyCount);
        SLANG_VK_RETURN_ON_FAIL(m_api.vkGetPhysicalDeviceCooperativeVectorPropertiesNV(
            m_api.m_physicalDevice,
            &vkPropertyCount,
            vkProperties.data()
        ));
        for (const auto& vkProps : vkProperties)
        {
            CooperativeVectorProperties props;
            props.inputType = VulkanUtil::translateCooperativeVectorComponentType(vkProps.inputType);
            props.inputInterpretation =
                VulkanUtil::translateCooperativeVectorComponentType(vkProps.inputInterpretation);
            props.matrixInterpretation =
                VulkanUtil::translateCooperativeVectorComponentType(vkProps.matrixInterpretation);
            props.biasInterpretation = VulkanUtil::translateCooperativeVectorComponentType(vkProps.biasInterpretation);
            props.resultType = VulkanUtil::translateCooperativeVectorComponentType(vkProps.resultType);
            props.transpose = vkProps.transpose;
            m_cooperativeVectorProperties.push_back(props);
        }
    }

    return Device::getCooperativeVectorProperties(properties, propertyCount);
}

Result DeviceImpl::convertCooperativeVectorMatrix(const ConvertCooperativeVectorMatrixDesc* descs, uint32_t descCount)
{
    if (!m_api.m_extendedFeatures.cooperativeVectorFeatures.cooperativeVector ||
        !m_api.vkConvertCooperativeVectorMatrixNV)
        return SLANG_E_NOT_AVAILABLE;

    for (uint32_t i = 0; i < descCount; ++i)
    {
        VkConvertCooperativeVectorMatrixInfoNV info = VulkanUtil::translateConvertCooperativeVectorMatrixDesc(descs[i]);
        SLANG_VK_RETURN_ON_FAIL(m_api.vkConvertCooperativeVectorMatrixNV(m_api.m_device, &info));
    }

    return SLANG_OK;
}

Result DeviceImpl::getFormatSupport(Format format, FormatSupport* outFormatSupport)
{
    VkFormat vkFormat = VulkanUtil::getVkFormat(format);

    VkFormatProperties props = {};
    m_api.vkGetPhysicalDeviceFormatProperties(m_api.m_physicalDevice, vkFormat, &props);

    FormatSupport support = FormatSupport::None;

    if (props.bufferFeatures)
        support = support | FormatSupport::Buffer;

    if (format == Format::R32_UINT || format == Format::R16_UINT)
    {
        // There is no explicit bit in vk::FormatFeatureFlags for index buffers
        support = support | FormatSupport::IndexBuffer;
    }

    if (props.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)
        support = support | FormatSupport::VertexBuffer;

    if (props.optimalTilingFeatures)
        support = support | FormatSupport::Texture;

    if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        support = support | FormatSupport::DepthStencil;

    if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
        support = support | FormatSupport::RenderTarget;

    if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
        support = support | FormatSupport::Blendable;

    if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ||
        (props.bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT))
    {
        support = support | FormatSupport::ShaderLoad;
    }

    *outFormatSupport = support;
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
        dstDesc.format = VulkanUtil::getVkFormat(srcDesc.format);
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
    RefPtr<ShaderProgramImpl> shaderProgram = new ShaderProgramImpl(this);
    shaderProgram->init(desc);
    SLANG_RETURN_ON_FAIL(RootShaderObjectLayoutImpl::create(
        this,
        shaderProgram->linkedProgram,
        shaderProgram->linkedProgram->getLayout(),
        shaderProgram->m_rootShaderObjectLayout.writeRef()
    ));
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
    returnRefPtrMove(outLayout, layout);
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
    RefPtr<ShaderTableImpl> result = new ShaderTableImpl();
    result->m_device = this;
    result->init(desc);
    returnComPtr(outShaderTable, result);
    return SLANG_OK;
}

Result DeviceImpl::waitForFences(
    uint32_t fenceCount,
    IFence** fences,
    uint64_t* fenceValues,
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
    return result == VK_SUCCESS ? SLANG_OK : SLANG_FAIL;
}

} // namespace rhi::vk
