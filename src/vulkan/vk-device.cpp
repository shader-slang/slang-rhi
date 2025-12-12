#include "vk-device.h"
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

#include "cooperative-vector-utils.h"

#include "core/common.h"
#include "core/short_vector.h"
#include "core/static_vector.h"
#include "core/deferred.h"

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

inline Result getAdaptersImpl(std::vector<AdapterImpl>& outAdapters)
{
    VulkanModule module;
    SLANG_RETURN_ON_FAIL(module.init());
    SLANG_RHI_DEFERRED({ module.destroy(); });

    VulkanApi api;
    SLANG_RETURN_ON_FAIL(api.initGlobalProcs(module));

    VkInstanceCreateInfo instanceCreateInfo = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    const char* instanceExtensions[] = {
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
#if SLANG_APPLE_FAMILY
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
#endif
    };
    instanceCreateInfo.enabledExtensionCount = SLANG_COUNT_OF(instanceExtensions);
    instanceCreateInfo.ppEnabledExtensionNames = &instanceExtensions[0];
#if SLANG_APPLE_FAMILY
    instanceCreateInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    VkInstance instance;
    SLANG_VK_RETURN_ON_FAIL(api.vkCreateInstance(&instanceCreateInfo, nullptr, &instance));
    SLANG_RHI_DEFERRED({ api.vkDestroyInstance(instance, nullptr); });

    // This will fail due to not loading any extensions.
    api.initInstanceProcs(instance);

    if (!(api.vkEnumeratePhysicalDevices && api.vkGetPhysicalDeviceProperties2 &&
          api.vkEnumerateDeviceExtensionProperties))
    {
        return SLANG_FAIL;
    }

    uint32_t physicalDeviceCount = 0;
    SLANG_VK_RETURN_ON_FAIL(api.vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr));
    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    SLANG_VK_RETURN_ON_FAIL(api.vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data()));

    for (const auto& physicalDevice : physicalDevices)
    {
        VkPhysicalDeviceIDProperties idProps = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        props.pNext = &idProps;
        SLANG_RHI_ASSERT(api.vkGetPhysicalDeviceFeatures2);
        api.vkGetPhysicalDeviceProperties2(physicalDevice, &props);

        AdapterInfo info = {};
        info.deviceType = DeviceType::Vulkan;
        switch (props.properties.deviceType)
        {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            info.adapterType = AdapterType::Discrete;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            info.adapterType = AdapterType::Integrated;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            info.adapterType = AdapterType::Software;
            break;
        default:
            info.adapterType = AdapterType::Unknown;
            break;
        }
        string::copy_safe(info.name, sizeof(info.name), props.properties.deviceName);
        info.vendorID = props.properties.vendorID;
        info.deviceID = props.properties.deviceID;
        info.luid = getAdapterLUID(idProps);

        AdapterImpl adapter;
        adapter.m_info = info;
        memcpy(adapter.m_deviceUUID, idProps.deviceUUID, VK_UUID_SIZE);

        outAdapters.push_back(adapter);
    }

    // Mark default adapter (prefer discrete if available).
    markDefaultAdapter(outAdapters);

    return SLANG_OK;
}

std::vector<AdapterImpl>& getAdapters()
{
    static std::vector<AdapterImpl> adapters;
    static Result initResult = getAdaptersImpl(adapters);
    SLANG_UNUSED(initResult);
    return adapters;
}

DeviceImpl::DeviceImpl() {}

DeviceImpl::~DeviceImpl()
{
    // Wait for all commands to finish and retire any active command buffers.
    if (m_queue)
    {
        m_queue->waitOnHost();
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

    m_bindlessDescriptorSet.setNull();

    if (m_api.vkDestroySampler)
    {
        m_api.vkDestroySampler(m_device, m_defaultSampler, nullptr);
    }

    m_queue.setNull();
    m_deviceQueue.destroy();

    descriptorSetAllocator.close();

    if (m_device != VK_NULL_HANDLE)
    {
        if (!m_existingDeviceHandles.handles[2])
            m_api.vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
        if (m_debugReportCallback != VK_NULL_HANDLE)
            m_api.vkDestroyDebugUtilsMessengerEXT(m_api.m_instance, m_debugReportCallback, nullptr);
        if (m_api.m_instance != VK_NULL_HANDLE && !m_existingDeviceHandles.handles[0])
            m_api.vkDestroyInstance(m_api.m_instance, nullptr);
    }
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
    // https://github.com/shader-slang/slang/issues/9106
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
    const DeviceDesc& desc,
    bool enableValidationLayer,
    std::vector<Feature>& availableFeatures,
    std::vector<Capability>& availableCapabilities
)
{
    availableFeatures.clear();
    availableCapabilities.clear();

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

        static_vector<const char*, 16> instanceExtensions;

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
        instanceExtensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#endif

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

                if (m_extendedDesc.enableDebugPrintf)
                {
                    // Include support for printf
                    validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
                    validationFeatures.enabledValidationFeatureCount = 1;
                    validationFeatures.pEnabledValidationFeatures = enabledValidationFeatures;
                    instanceCreateInfo.pNext = &validationFeatures;
                }
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

    if ((desc.enableRayTracingValidation || enableValidationLayer) && m_api.vkCreateDebugUtilsMessengerEXT)
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

    // Initialize Vulkan physical device.
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    if (!desc.existingDeviceHandles.handles[1])
    {
        AdapterImpl* adapter = nullptr;
        SLANG_RETURN_ON_FAIL(selectAdapter(this, getAdapters(), desc, adapter));

        uint32_t physicalDeviceCount = 0;
        SLANG_VK_RETURN_ON_FAIL(m_api.vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr));
        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        SLANG_VK_RETURN_ON_FAIL(
            m_api.vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data())
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
    SLANG_RETURN_ON_FAIL(m_api.initPhysicalDevice(physicalDevice));

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

    // Get the API version
    const uint32_t majorVersion = VK_VERSION_MAJOR(basicProps.apiVersion);
    const uint32_t minorVersion = VK_VERSION_MINOR(basicProps.apiVersion);

    auto& extendedFeatures = m_api.m_extendedFeatures;

#define EXTEND_DESC_CHAIN(head_, desc_)                                                                                \
    {                                                                                                                  \
        desc_.pNext = head_.pNext;                                                                                     \
        head_.pNext = &desc_;                                                                                          \
    }

    // API version check, can't use vkGetPhysicalDeviceProperties2 yet since this device might not
    // support it
    if (VK_MAKE_VERSION(majorVersion, minorVersion, 0) >= VK_API_VERSION_1_1 && m_api.vkGetPhysicalDeviceProperties2 &&
        m_api.vkGetPhysicalDeviceFeatures2)
    {
        // Get device features
        VkPhysicalDeviceFeatures2 deviceFeatures2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.inlineUniformBlockFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.rayTracingPipelineFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.rayQueryFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.rayTracingPositionFetchFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.rayTracingMotionBlurFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.rayTracingInvocationReorderFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.accelerationStructureFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.variablePointersFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.computeShaderDerivativesFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.extendedDynamicStateFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.storage16BitFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.robustness2Features);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.clockFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.atomicFloatFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.atomicFloat2Features);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.imageInt64AtomicFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.meshShaderFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.multiviewFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.fragmentShadingRateFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.rayTracingValidationFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.shaderDrawParametersFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.dynamicRenderingFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.customBorderColorFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.dynamicRenderingLocalReadFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.formats4444Features);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.shaderMaximalReconvergenceFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.shaderQuadControlFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.shaderIntegerDotProductFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.cooperativeVectorFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.rayTracingLinearSweptSpheresFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.clusterAccelerationStructureFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.cooperativeMatrix1Features);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.descriptorIndexingFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.mutableDescriptorTypeFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.pipelineBinaryFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.shaderSubgroupRotateFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.shaderReplicatedCompositesFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.fragmentShaderBarycentricFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.vertexAttributeRobustnessFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.fragmentShaderInterlockFeatures);
        EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.shaderDemoteToHelperInvocationFeatures);

        if (VK_MAKE_VERSION(majorVersion, minorVersion, 0) >= VK_API_VERSION_1_2)
        {
            EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.vulkan12Features);
        }

        if (VK_MAKE_VERSION(majorVersion, minorVersion, 0) >= VK_API_VERSION_1_3)
        {
            EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.vulkan13Features);
        }

        if (VK_MAKE_VERSION(majorVersion, minorVersion, 0) >= VK_API_VERSION_1_4)
        {
            EXTEND_DESC_CHAIN(deviceFeatures2, extendedFeatures.vulkan14Features);
        }

        m_api.vkGetPhysicalDeviceFeatures2(m_api.m_physicalDevice, &deviceFeatures2);

        if (deviceFeatures2.features.shaderResourceMinLod)
        {
            availableFeatures.push_back(Feature::ShaderResourceMinLod);
            availableCapabilities.push_back(Capability::spvMinLod);
        }
        if (deviceFeatures2.features.shaderFloat64)
        {
            availableFeatures.push_back(Feature::Double);
        }
        if (deviceFeatures2.features.shaderInt64)
        {
            availableFeatures.push_back(Feature::Int64);
        }
        if (deviceFeatures2.features.shaderInt16)
        {
            availableFeatures.push_back(Feature::Int16);
        }
        // If we have float16 features then enable
        if (extendedFeatures.vulkan12Features.shaderFloat16)
        {
            availableFeatures.push_back(Feature::Half);
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

        // SIMPLE_EXTENSION_FEATURE(struct, feature member name, extension, code)
        // will check for the presence of the boolean feature member in struct and the availability of the extensions.
        // If they are both present then the extension is added, the struct linked into the deviceCreateInfo chain
        // and the code block is executed.
#define SIMPLE_EXTENSION_FEATURE(s, m, e, code)                                                                        \
    {                                                                                                                  \
        if (addFeatureExtension(s.m, s, e))                                                                            \
        {                                                                                                              \
            code                                                                                                       \
        }                                                                                                              \
    }

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.shaderDrawParametersFeatures,
            shaderDrawParameters,
            VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
            {}
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.dynamicRenderingFeatures,
            dynamicRendering,
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
            {/* "dynamic-rendering" */}
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.dynamicRenderingLocalReadFeatures,
            dynamicRenderingLocalRead,
            VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME,
            {/* "dynamic-rendering" */}
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.formats4444Features,
            formatA4R4G4B4,
            VK_EXT_4444_FORMATS_EXTENSION_NAME,
            {/* "b4g4r4a4-format" */}
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.storage16BitFeatures,
            storageBuffer16BitAccess,
            VK_KHR_16BIT_STORAGE_EXTENSION_NAME,
            {/* "16-bit-storage" */}
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.atomicFloatFeatures,
            shaderBufferFloat32Atomics,
            VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME,
            {
                availableFeatures.push_back(Feature::AtomicFloat);
                availableCapabilities.push_back(Capability::SPV_EXT_shader_atomic_float_add);
            }
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.atomicFloat2Features,
            shaderBufferFloat16Atomics,
            VK_EXT_SHADER_ATOMIC_FLOAT_2_EXTENSION_NAME,
            {
                availableFeatures.push_back(Feature::AtomicFloat);
                availableCapabilities.push_back(Capability::SPV_EXT_shader_atomic_float16_add);
                availableCapabilities.push_back(Capability::SPV_EXT_shader_atomic_float_min_max);
            }
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.imageInt64AtomicFeatures,
            shaderImageInt64Atomics,
            VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME,
            {/* "image-atomic-int64" */}
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.extendedDynamicStateFeatures,
            extendedDynamicState,
            VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
            {/* "extended-dynamic-states" */}
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.customBorderColorFeatures,
            customBorderColors,
            VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME,
            { availableFeatures.push_back(Feature::CustomBorderColor); }
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.vertexAttributeRobustnessFeatures,
            vertexAttributeRobustness,
            VK_EXT_VERTEX_ATTRIBUTE_ROBUSTNESS_EXTENSION_NAME,
            {/* Enable vertex attribute robustness to avoid validation errors */}
        );

        if (extendedFeatures.vulkan14Features.shaderSubgroupRotate &&
            extendedFeatures.vulkan14Features.shaderSubgroupRotateClustered &&
            extensionNames.count(VK_KHR_SHADER_SUBGROUP_ROTATE_EXTENSION_NAME))
        {
            extendedFeatures.shaderSubgroupRotateFeatures.shaderSubgroupRotate = VK_TRUE;
            extendedFeatures.shaderSubgroupRotateFeatures.shaderSubgroupRotateClustered = VK_TRUE;
            extendedFeatures.shaderSubgroupRotateFeatures.pNext = (void*)deviceCreateInfo.pNext;
            deviceCreateInfo.pNext = &extendedFeatures.shaderSubgroupRotateFeatures;
            deviceExtensions.push_back(VK_KHR_SHADER_SUBGROUP_ROTATE_EXTENSION_NAME);
        }

        if (extendedFeatures.accelerationStructureFeatures.accelerationStructure &&
            extensionNames.count(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
            extensionNames.count(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME))
        {
            extendedFeatures.accelerationStructureFeatures.pNext = (void*)deviceCreateInfo.pNext;
            deviceCreateInfo.pNext = &extendedFeatures.accelerationStructureFeatures;
            deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            availableFeatures.push_back(Feature::AccelerationStructure);

            // These both depend on VK_KHR_acceleration_structure

            SIMPLE_EXTENSION_FEATURE(
                extendedFeatures.rayTracingPipelineFeatures,
                rayTracingPipeline,
                VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
                {
                    availableFeatures.push_back(Feature::RayTracing);
                    availableCapabilities.push_back(Capability::SPV_KHR_ray_tracing);
                    availableCapabilities.push_back(Capability::spvRayTracingKHR);
                }
            );

            SIMPLE_EXTENSION_FEATURE(
                extendedFeatures.rayTracingPositionFetchFeatures,
                rayTracingPositionFetch,
                VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME,
                {
                    availableCapabilities.push_back(Capability::SPV_KHR_ray_tracing_position_fetch);
                    availableCapabilities.push_back(Capability::spvRayTracingPositionFetchKHR);
                    if (extendedFeatures.rayQueryFeatures.rayQuery)
                    {
                        availableCapabilities.push_back(Capability::spvRayQueryPositionFetchKHR);
                    }
                }
            );

            SIMPLE_EXTENSION_FEATURE(extendedFeatures.rayQueryFeatures, rayQuery, VK_KHR_RAY_QUERY_EXTENSION_NAME, {
                availableFeatures.push_back(Feature::RayQuery);
                availableCapabilities.push_back(Capability::SPV_KHR_ray_query);
                availableCapabilities.push_back(Capability::spvRayQueryKHR);
            });

            if ((extendedFeatures.rayTracingLinearSweptSpheresFeatures.spheres ||
                 extendedFeatures.rayTracingLinearSweptSpheresFeatures.linearSweptSpheres) &&
                extensionNames.count(VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME))
            {
                extendedFeatures.rayTracingLinearSweptSpheresFeatures.pNext = (void*)deviceCreateInfo.pNext;
                deviceCreateInfo.pNext = &extendedFeatures.rayTracingLinearSweptSpheresFeatures;
                deviceExtensions.push_back(VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME);
                if (extendedFeatures.rayTracingLinearSweptSpheresFeatures.spheres)
                {
                    availableFeatures.push_back(Feature::AccelerationStructureSpheres);
                }
                if (extendedFeatures.rayTracingLinearSweptSpheresFeatures.linearSweptSpheres)
                {
                    availableFeatures.push_back(Feature::AccelerationStructureLinearSweptSpheres);
                    availableCapabilities.push_back(Capability::SPV_NV_linear_swept_spheres);
                    availableCapabilities.push_back(Capability::spvRayTracingLinearSweptSpheresGeometryNV);
                }
            }

            SIMPLE_EXTENSION_FEATURE(
                extendedFeatures.clusterAccelerationStructureFeatures,
                clusterAccelerationStructure,
                VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                {
                    availableFeatures.push_back(Feature::ClusterAccelerationStructure);
                    availableCapabilities.push_back(Capability::SPV_NV_cluster_acceleration_structure);
                    availableCapabilities.push_back(Capability::spvRayTracingClusterAccelerationStructureNV);
                }
            );
        }

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.inlineUniformBlockFeatures,
            inlineUniformBlock,
            VK_EXT_INLINE_UNIFORM_BLOCK_EXTENSION_NAME,
            {/* "inline-uniform-block" */}
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.robustness2Features,
            nullDescriptor,
            VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
            {/* "robustness2" */}
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.clockFeatures,
            shaderDeviceClock,
            VK_KHR_SHADER_CLOCK_EXTENSION_NAME,
            {
                availableFeatures.push_back(Feature::RealtimeClock);
                availableCapabilities.push_back(Capability::SPV_KHR_shader_clock);
                availableCapabilities.push_back(Capability::spvShaderClockKHR);
            }
        );

        SIMPLE_EXTENSION_FEATURE(extendedFeatures.meshShaderFeatures, meshShader, VK_EXT_MESH_SHADER_EXTENSION_NAME, {
            availableFeatures.push_back(Feature::MeshShader);
            availableCapabilities.push_back(Capability::SPV_EXT_mesh_shader);
            availableCapabilities.push_back(Capability::spvMeshShadingEXT);
        });

        SIMPLE_EXTENSION_FEATURE(extendedFeatures.multiviewFeatures, multiview, VK_KHR_MULTIVIEW_EXTENSION_NAME, {
            availableFeatures.push_back(Feature::MultiView);
        });

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.fragmentShadingRateFeatures,
            primitiveFragmentShadingRate,
            VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME,
            { availableFeatures.push_back(Feature::FragmentShadingRate); }
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.rayTracingInvocationReorderFeatures,
            rayTracingInvocationReorder,
            VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME,
            {
                availableFeatures.push_back(Feature::ShaderExecutionReordering);
                availableCapabilities.push_back(Capability::SPV_NV_shader_invocation_reorder);
                availableCapabilities.push_back(Capability::spvShaderInvocationReorderNV);
                availableCapabilities.push_back(Capability::spirv_nv);
            }
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.variablePointersFeatures,
            variablePointers,
            VK_KHR_VARIABLE_POINTERS_EXTENSION_NAME,
            {/* "variable-pointer" */}
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.computeShaderDerivativesFeatures,
            computeDerivativeGroupQuads,
            VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME,
            { availableCapabilities.push_back(Capability::SPV_KHR_compute_shader_derivatives); }
        );
        // Also enable VK_NV_compute_shader_derivatives even if VK_KHR_compute_shader_derivatives is available.
        // This avoids warnings from the validation layer if the NV specific intrinsics are used.
        if (extensionNames.count(VK_NV_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_NV_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
        }

        // Only enable raytracing validation if both requested and supported
        if (desc.enableRayTracingValidation && extendedFeatures.rayTracingValidationFeatures.rayTracingValidation)
        {
            SIMPLE_EXTENSION_FEATURE(
                extendedFeatures.rayTracingValidationFeatures,
                rayTracingValidation,
                VK_NV_RAY_TRACING_VALIDATION_EXTENSION_NAME,
                { availableFeatures.push_back(Feature::RayTracingValidation); }
            );
        }

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.shaderMaximalReconvergenceFeatures,
            shaderMaximalReconvergence,
            VK_KHR_SHADER_MAXIMAL_RECONVERGENCE_EXTENSION_NAME,
            {
                availableCapabilities.push_back(Capability::SPV_KHR_maximal_reconvergence);
                availableCapabilities.push_back(Capability::spvMaximalReconvergenceKHR);
            }
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.shaderQuadControlFeatures,
            shaderQuadControl,
            VK_KHR_SHADER_QUAD_CONTROL_EXTENSION_NAME,
            {
                availableCapabilities.push_back(Capability::SPV_KHR_quad_control);
                availableCapabilities.push_back(Capability::spvQuadControlKHR);
            }
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.shaderIntegerDotProductFeatures,
            shaderIntegerDotProduct,
            VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME,
            {/* "integer-dot-product" */}
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.cooperativeVectorFeatures,
            cooperativeVector,
            VK_NV_COOPERATIVE_VECTOR_EXTENSION_NAME,
            {
                availableFeatures.push_back(Feature::CooperativeVector);
                availableCapabilities.push_back(Capability::SPV_NV_cooperative_vector);
                availableCapabilities.push_back(Capability::spvCooperativeVectorNV);
                if (extendedFeatures.cooperativeVectorFeatures.cooperativeVectorTraining)
                {
                    availableCapabilities.push_back(Capability::spvCooperativeVectorTrainingNV);
                }
            }
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.cooperativeMatrix1Features,
            cooperativeMatrix,
            VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME,
            {
                availableFeatures.push_back(Feature::CooperativeMatrix);
                availableCapabilities.push_back(Capability::SPV_KHR_cooperative_matrix);
                availableCapabilities.push_back(Capability::spvCooperativeMatrixKHR);
            }
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.mutableDescriptorTypeFeatures,
            mutableDescriptorType,
            VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME,
            {}
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.pipelineBinaryFeatures,
            pipelineBinaries,
            VK_KHR_PIPELINE_BINARY_EXTENSION_NAME,
            {
                deviceExtensions.push_back(VK_KHR_MAINTENANCE_5_EXTENSION_NAME);
                availableFeatures.push_back(Feature::PipelineCache);
            }
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.shaderReplicatedCompositesFeatures,
            shaderReplicatedComposites,
            VK_EXT_SHADER_REPLICATED_COMPOSITES_EXTENSION_NAME,
            {
                availableCapabilities.push_back(Capability::SPV_EXT_replicated_composites);
                availableCapabilities.push_back(Capability::spvReplicatedCompositesEXT);
            }
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.fragmentShaderBarycentricFeatures,
            fragmentShaderBarycentric,
            VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME,
            {
                availableFeatures.push_back(Feature::Barycentrics);
                availableCapabilities.push_back(Capability::SPV_KHR_fragment_shader_barycentric);
                availableCapabilities.push_back(Capability::spvFragmentBarycentricKHR);
            }
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.shaderDemoteToHelperInvocationFeatures,
            shaderDemoteToHelperInvocation,
            VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME,
            {
                availableCapabilities.push_back(Capability::SPV_EXT_demote_to_helper_invocation);
                availableCapabilities.push_back(Capability::spvDemoteToHelperInvocationEXT);
            }
        );

        SIMPLE_EXTENSION_FEATURE(
            extendedFeatures.rayTracingMotionBlurFeatures,
            rayTracingMotionBlur,
            VK_NV_RAY_TRACING_MOTION_BLUR_EXTENSION_NAME,
            {
                availableFeatures.push_back(Feature::RayTracingMotionBlur);
                availableCapabilities.push_back(Capability::SPV_NV_ray_tracing_motion_blur);
                availableCapabilities.push_back(Capability::spvRayTracingMotionBlurNV);
            }
        )

#undef SIMPLE_EXTENSION_FEATURE

        if (extendedFeatures.vulkan12Features.shaderBufferInt64Atomics)
            availableFeatures.push_back(Feature::AtomicInt64);

        if (_hasAnySetBits(
                extendedFeatures.vulkan12Features,
                offsetof(VkPhysicalDeviceVulkan12Features, pNext) + sizeof(void*)
            ))
        {
            extendedFeatures.vulkan12Features.pNext = (void*)deviceCreateInfo.pNext;
            deviceCreateInfo.pNext = &extendedFeatures.vulkan12Features;
        }

        VkPhysicalDeviceProperties2 extendedProps = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtpProps = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR
        };
        VkPhysicalDeviceSubgroupProperties subgroupProps = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};

        EXTEND_DESC_CHAIN(extendedProps, rtpProps);
        EXTEND_DESC_CHAIN(extendedProps, subgroupProps);

        m_api.vkGetPhysicalDeviceProperties2(m_api.m_physicalDevice, &extendedProps);
        m_api.m_rayTracingPipelineProperties = rtpProps;

        // Approximate DX12's WaveOps boolean
        if (subgroupProps.supportedOperations &
            (VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT | VK_SUBGROUP_FEATURE_ARITHMETIC_BIT |
             VK_SUBGROUP_FEATURE_BALLOT_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
             VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT | VK_SUBGROUP_FEATURE_CLUSTERED_BIT |
             VK_SUBGROUP_FEATURE_QUAD_BIT | VK_SUBGROUP_FEATURE_PARTITIONED_BIT_NV))
        {
            availableFeatures.push_back(Feature::WaveOps);
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
        }
        if (extensionNames.count(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);
            availableFeatures.push_back(Feature::ConservativeRasterization);
            availableCapabilities.push_back(Capability::SPV_EXT_fragment_fully_covered);
            availableCapabilities.push_back(Capability::spvFragmentFullyCoveredEXT);
        }
        if (extensionNames.count(VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME);
            // availableFeatures.push_back(Feature::RasterizerOrderedViews); TODO should this be enabled?
            availableCapabilities.push_back(Capability::SPV_EXT_fragment_shader_interlock);
            if (extendedFeatures.fragmentShaderInterlockFeatures.fragmentShaderPixelInterlock)
            {
                availableCapabilities.push_back(Capability::spvFragmentShaderPixelInterlockEXT);
            }
        }
        if (extensionNames.count(VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME);
        }
        if (extensionNames.count(VK_NVX_BINARY_IMPORT_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_NVX_BINARY_IMPORT_EXTENSION_NAME);
        }
        if (extensionNames.count(VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME);
        }
        if (extensionNames.count(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
        }
        if (extensionNames.count(VK_NV_SHADER_SUBGROUP_PARTITIONED_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_NV_SHADER_SUBGROUP_PARTITIONED_EXTENSION_NAME);
        }
    }

    if (extendedFeatures.vulkan12Features.descriptorIndexing)
    {
        availableCapabilities.push_back(Capability::SPV_EXT_descriptor_indexing);
    }

    if (extendedFeatures.vulkan12Features.descriptorIndexing &&
        extendedFeatures.descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing &&
        extendedFeatures.descriptorIndexingFeatures.shaderStorageBufferArrayNonUniformIndexing &&
        extendedFeatures.descriptorIndexingFeatures.shaderStorageImageArrayNonUniformIndexing &&
        extendedFeatures.descriptorIndexingFeatures.shaderUniformTexelBufferArrayNonUniformIndexing &&
        extendedFeatures.descriptorIndexingFeatures.shaderStorageTexelBufferArrayNonUniformIndexing &&
        extendedFeatures.descriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind &&
        extendedFeatures.descriptorIndexingFeatures.descriptorBindingStorageImageUpdateAfterBind &&
        extendedFeatures.descriptorIndexingFeatures.descriptorBindingStorageBufferUpdateAfterBind &&
        extendedFeatures.descriptorIndexingFeatures.descriptorBindingUniformTexelBufferUpdateAfterBind &&
        extendedFeatures.descriptorIndexingFeatures.descriptorBindingStorageTexelBufferUpdateAfterBind &&
        extendedFeatures.descriptorIndexingFeatures.descriptorBindingUpdateUnusedWhilePending &&
        extendedFeatures.descriptorIndexingFeatures.descriptorBindingPartiallyBound &&
        extendedFeatures.mutableDescriptorTypeFeatures.mutableDescriptorType)
    {
        availableFeatures.push_back(Feature::Bindless);
    }

    if (extendedFeatures.vulkan13Features.shaderDemoteToHelperInvocation)
    {
        availableCapabilities.push_back(Capability::spvDemoteToHelperInvocation);
    }

    // Detect SPIRV version
    availableCapabilities.push_back(Capability::_spirv_1_0);
    if (VK_MAKE_VERSION(majorVersion, minorVersion, 0) >= VK_API_VERSION_1_1)
    {
        availableCapabilities.push_back(Capability::_spirv_1_1);
        availableCapabilities.push_back(Capability::_spirv_1_2);
        availableCapabilities.push_back(Capability::_spirv_1_3);
    }
    if (VK_MAKE_VERSION(majorVersion, minorVersion, 0) >= VK_API_VERSION_1_2)
    {
        availableCapabilities.push_back(Capability::_spirv_1_4);
        availableCapabilities.push_back(Capability::_spirv_1_5);
    }
    if (VK_MAKE_VERSION(majorVersion, minorVersion, 0) >= VK_API_VERSION_1_3)
    {
        availableCapabilities.push_back(Capability::_spirv_1_6);
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

    // Create Vulkan device.
    if (!desc.existingDeviceHandles.handles[2])
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
    SLANG_RETURN_ON_FAIL(m_api.initDeviceProcs(m_device));

    return SLANG_OK;
}

Result DeviceImpl::initialize(const DeviceDesc& desc)
{
    SLANG_RETURN_ON_FAIL(Device::initialize(desc));

    // During shutdown we need to know if existing device handles were used.
    m_existingDeviceHandles = desc.existingDeviceHandles;

    // Process chained descs
    for (const DescStructHeader* header = static_cast<const DescStructHeader*>(desc.next); header;
         header = header->next)
    {
        switch (header->type)
        {
        case StructType::VulkanDeviceExtendedDesc:
            memcpy(static_cast<void*>(&m_extendedDesc), header, sizeof(m_extendedDesc));
            break;
        default:
            break;
        }
    }

    std::vector<Feature> availableFeatures;
    std::vector<Capability> availableCapabilities;
    SLANG_RETURN_ON_FAIL(m_module.init());
    SLANG_RETURN_ON_FAIL(m_api.initGlobalProcs(m_module));
    descriptorSetAllocator.m_api = &m_api;
    SLANG_RETURN_ON_FAIL(
        initVulkanInstanceAndDevice(desc, isDebugLayersEnabled(), availableFeatures, availableCapabilities)
    );

    VkPhysicalDeviceIDProperties idProps = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props.pNext = &idProps;
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

        m_info.limits = limits;
    }

    // Initialize features & capabilities.
    bool isSoftwareDevice = (basicProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_OTHER) ||
                            (basicProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU);
    addFeature(isSoftwareDevice ? Feature::SoftwareDevice : Feature::HardwareDevice);
    addFeature(Feature::Surface);
    addFeature(Feature::ParameterBlock);
    addFeature(Feature::Rasterization);
    addFeature(Feature::CombinedTextureSampler);
    addFeature(Feature::TimestampQuery);
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
            if (int(feature) >= int(Feature::SM_6_0) && int(feature) <= int(Feature::SM_6_9))
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
        SLANG_VK_RETURN_ON_FAIL(m_api.vkCreateSampler(m_device, &samplerInfo, nullptr, &m_defaultSampler));
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
    m_queue->setInternalReferenceCount(1);

    return SLANG_OK;
}

void DeviceImpl::waitForGpu()
{
    m_deviceQueue.flushAndWait();
}

Result DeviceImpl::getQueue(QueueType type, ICommandQueue** outQueue)
{
    if (type != QueueType::Graphics)
        return SLANG_FAIL;
    m_queue->establishStrongReferenceToDevice();
    returnComPtr(outQueue, m_queue);
    return SLANG_OK;
}

Result DeviceImpl::readBuffer(IBuffer* buffer, Offset offset, Size size, void* outData)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    if (offset + size > bufferImpl->m_desc.size)
    {
        return SLANG_FAIL;
    }

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
    barrier.srcAccessMask = calcAccessFlags(bufferImpl->m_desc.defaultState);
    barrier.dstAccessMask = calcAccessFlags(ResourceState::CopyDestination);
    barrier.buffer = bufferImpl->m_buffer.m_buffer;
    barrier.offset = 0;
    barrier.size = bufferImpl->m_desc.size;

    VkPipelineStageFlags srcStageFlags = calcPipelineStageFlags(bufferImpl->m_desc.defaultState, true);
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
    SLANG_RETURN_ON_FAIL(createBuffer(bufferDesc, nullptr, (IBuffer**)result->m_buffer.writeRef()));
    VkAccelerationStructureCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    VkAccelerationStructureMotionInfoNV motionInfo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MOTION_INFO_NV};
    createInfo.buffer = result->m_buffer->m_buffer.m_buffer;
    createInfo.offset = 0;
    createInfo.size = desc.size;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR;
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

Result DeviceImpl::getTextureAllocationInfo(const TextureDesc& desc_, Size* outSize, Size* outAlignment)
{
    TextureDesc desc = fixupTextureDesc(desc_);

    const VkFormat format = getVkFormat(desc.format);
    if (format == VK_FORMAT_UNDEFINED)
    {
        SLANG_RHI_ASSERT_FAILURE("Unhandled image format");
        return SLANG_FAIL;
    }
    VkImageCreateInfo imageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    switch (desc.type)
    {
    case TextureType::Texture1D:
    case TextureType::Texture1DArray:
    {
        imageInfo.imageType = VK_IMAGE_TYPE_1D;
        imageInfo.extent = VkExtent3D{desc.size.width, 1, 1};
        break;
    }
    case TextureType::Texture2D:
    case TextureType::Texture2DArray:
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
    {
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = VkExtent3D{desc.size.width, desc.size.height, 1};
        break;
    }
    case TextureType::Texture3D:
    {
        // Can't have an array and 3d texture
        SLANG_RHI_ASSERT(desc.arrayLength <= 1);
        imageInfo.imageType = VK_IMAGE_TYPE_3D;
        imageInfo.extent = VkExtent3D{desc.size.width, desc.size.height, desc.size.depth};
        break;
    }
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
    {
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = VkExtent3D{desc.size.width, desc.size.height, 1};
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        break;
    }
    }

    imageInfo.mipLevels = desc.mipCount;
    imageInfo.arrayLayers = desc.getLayerCount();

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

Result DeviceImpl::getTextureRowAlignment(Format format, Size* outAlignment)
{
    switch (format)
    {
    case Format::D16Unorm:
    case Format::D32Float:
    case Format::D32FloatS8Uint:
        *outAlignment = 4;
        break;
    default:
        *outAlignment = 1;
        break;
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
        SLANG_VK_RETURN_ON_FAIL(m_api.vkGetPhysicalDeviceCooperativeVectorPropertiesNV(
            m_api.m_physicalDevice,
            &vkPropertyCount,
            vkProperties.data()
        ));
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
    SLANG_VK_RETURN_ON_FAIL(m_api.vkConvertCooperativeVectorMatrixNV(m_api.m_device, &info));
    *outSize = math::calcAligned(*outSize, 64);
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
        SLANG_VK_RETURN_ON_FAIL(m_api.vkConvertCooperativeVectorMatrixNV(m_api.m_device, &info));
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
    return result == VK_SUCCESS ? SLANG_OK : SLANG_FAIL;
}

} // namespace rhi::vk

namespace rhi {

IAdapter* getVKAdapter(uint32_t index)
{
    std::vector<vk::AdapterImpl>& adapters = vk::getAdapters();
    return index < adapters.size() ? &adapters[index] : nullptr;
}

Result createVKDevice(const DeviceDesc* desc, IDevice** outDevice)
{
    RefPtr<vk::DeviceImpl> result = new vk::DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(*desc));
    returnComPtr(outDevice, result);
    return SLANG_OK;
}

} // namespace rhi
