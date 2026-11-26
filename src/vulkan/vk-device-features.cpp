#include "vk-device-features.h"

#include <cstring>
#include <vector>

namespace rhi::vk {

// Returns the structure with the given type in a Vulkan pNext chain, or nullptr if there is none.
template<typename T>
static const T* findInChain(const void* chain, VkStructureType sType)
{
    for (const VkBaseInStructure* s = static_cast<const VkBaseInStructure*>(chain); s; s = s->pNext)
        if (s->sType == sType)
            return reinterpret_cast<const T*>(s);
    return nullptr;
}

// Vulkan feature structures consist of sType and pNext followed by VkBool32 members only.
static constexpr size_t kFeatureBitsOffset = sizeof(VkBaseOutStructure);

// Copies the feature bits from another structure of the same type, leaving sType and pNext untouched.
template<typename T>
static void copyFeatureBits(T& dst, const T& src)
{
    std::memcpy(
        reinterpret_cast<uint8_t*>(&dst) + kFeatureBitsOffset,
        reinterpret_cast<const uint8_t*>(&src) + kFeatureBitsOffset,
        sizeof(T) - kFeatureBitsOffset
    );
}

// Clears all feature bits in `features` that are not set in `enabled`.
// `offset` is the offset of the first feature bit (0 for VkPhysicalDeviceFeatures).
template<typename T>
static void restrictFeatureBits(T& features, const T& enabled, size_t offset = kFeatureBitsOffset)
{
    VkBool32* dst = reinterpret_cast<VkBool32*>(reinterpret_cast<uint8_t*>(&features) + offset);
    const VkBool32* src = reinterpret_cast<const VkBool32*>(reinterpret_cast<const uint8_t*>(&enabled) + offset);
    for (size_t i = 0; i < (sizeof(T) - offset) / sizeof(VkBool32); i++)
        dst[i] = (dst[i] && src[i]) ? VK_TRUE : VK_FALSE;
}

// Device extensions that feature detection checks by name, together with the core version they were promoted to.
// A device using that core version provides their functionality without enabling the extension
// (their features still need to be enabled).
static const std::pair<const char*, uint32_t> kPromotedDeviceExtensions[] = {
    {VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME, VK_API_VERSION_1_1},
    {VK_KHR_16BIT_STORAGE_EXTENSION_NAME, VK_API_VERSION_1_1},
    {VK_KHR_MULTIVIEW_EXTENSION_NAME, VK_API_VERSION_1_1},
    {VK_KHR_VARIABLE_POINTERS_EXTENSION_NAME, VK_API_VERSION_1_1},
    {VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, VK_API_VERSION_1_3},
    {VK_EXT_INLINE_UNIFORM_BLOCK_EXTENSION_NAME, VK_API_VERSION_1_3},
    {VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME, VK_API_VERSION_1_3},
    {VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME, VK_API_VERSION_1_3},
    {VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME, VK_API_VERSION_1_3},
    {VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME, VK_API_VERSION_1_4},
    {VK_KHR_SHADER_SUBGROUP_ROTATE_EXTENSION_NAME, VK_API_VERSION_1_4},
};

// Collects the features that were enabled on an imported device from the VkDeviceCreateInfo it was created with.
// The features of a core version (VkPhysicalDeviceVulkanXXFeatures) and of the extensions promoted to it are
// merged, so a feature is recognized regardless of which structure was used to enable it.
static VulkanExtendedFeatures getEnabledFeatures(
    const VkDeviceCreateInfo& createInfo,
    const std::set<std::string>& enabledExtensions,
    VkPhysicalDeviceFeatures& outFeatures
)
{
    const void* chain = createInfo.pNext;

    outFeatures = {};
    if (createInfo.pEnabledFeatures)
        outFeatures = *createInfo.pEnabledFeatures;
    else if (auto f = findInChain<VkPhysicalDeviceFeatures2>(chain, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2))
        outFeatures = f->features;

    VulkanExtendedFeatures e;
#define COPY_ENABLED_FEATURES(name)                                                                                    \
    if (auto s = findInChain<decltype(e.name)>(chain, e.name.sType))                                                   \
        copyFeatureBits(e.name, *s);
    VK_EXTENDED_FEATURE_STRUCTS(COPY_ENABLED_FEATURES)
    COPY_ENABLED_FEATURES(vulkan12Features)
    COPY_ENABLED_FEATURES(vulkan13Features)
    COPY_ENABLED_FEATURES(vulkan14Features)
#undef COPY_ENABLED_FEATURES

    // Merges a feature that can be enabled through either of two structures.
    auto merge = [](VkBool32& a, VkBool32& b)
    {
        a = b = (a || b) ? VK_TRUE : VK_FALSE;
    };

    // Vulkan 1.1 (VulkanExtendedFeatures only contains the structures of the promoted extensions).
    if (auto v11 =
            findInChain<VkPhysicalDeviceVulkan11Features>(chain, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES))
    {
        e.storage16BitFeatures.storageBuffer16BitAccess |= v11->storageBuffer16BitAccess;
        e.storage16BitFeatures.uniformAndStorageBuffer16BitAccess |= v11->uniformAndStorageBuffer16BitAccess;
        e.storage16BitFeatures.storagePushConstant16 |= v11->storagePushConstant16;
        e.storage16BitFeatures.storageInputOutput16 |= v11->storageInputOutput16;
        e.multiviewFeatures.multiview |= v11->multiview;
        e.multiviewFeatures.multiviewGeometryShader |= v11->multiviewGeometryShader;
        e.multiviewFeatures.multiviewTessellationShader |= v11->multiviewTessellationShader;
        e.variablePointersFeatures.variablePointersStorageBuffer |= v11->variablePointersStorageBuffer;
        e.variablePointersFeatures.variablePointers |= v11->variablePointers;
        e.shaderDrawParametersFeatures.shaderDrawParameters |= v11->shaderDrawParameters;
    }

    // Vulkan 1.2
    auto& v12 = e.vulkan12Features;
    auto& di = e.descriptorIndexingFeatures;
    merge(v12.shaderInputAttachmentArrayDynamicIndexing, di.shaderInputAttachmentArrayDynamicIndexing);
    merge(v12.shaderUniformTexelBufferArrayDynamicIndexing, di.shaderUniformTexelBufferArrayDynamicIndexing);
    merge(v12.shaderStorageTexelBufferArrayDynamicIndexing, di.shaderStorageTexelBufferArrayDynamicIndexing);
    merge(v12.shaderUniformBufferArrayNonUniformIndexing, di.shaderUniformBufferArrayNonUniformIndexing);
    merge(v12.shaderSampledImageArrayNonUniformIndexing, di.shaderSampledImageArrayNonUniformIndexing);
    merge(v12.shaderStorageBufferArrayNonUniformIndexing, di.shaderStorageBufferArrayNonUniformIndexing);
    merge(v12.shaderStorageImageArrayNonUniformIndexing, di.shaderStorageImageArrayNonUniformIndexing);
    merge(v12.shaderInputAttachmentArrayNonUniformIndexing, di.shaderInputAttachmentArrayNonUniformIndexing);
    merge(v12.shaderUniformTexelBufferArrayNonUniformIndexing, di.shaderUniformTexelBufferArrayNonUniformIndexing);
    merge(v12.shaderStorageTexelBufferArrayNonUniformIndexing, di.shaderStorageTexelBufferArrayNonUniformIndexing);
    merge(v12.descriptorBindingUniformBufferUpdateAfterBind, di.descriptorBindingUniformBufferUpdateAfterBind);
    merge(v12.descriptorBindingSampledImageUpdateAfterBind, di.descriptorBindingSampledImageUpdateAfterBind);
    merge(v12.descriptorBindingStorageImageUpdateAfterBind, di.descriptorBindingStorageImageUpdateAfterBind);
    merge(v12.descriptorBindingStorageBufferUpdateAfterBind, di.descriptorBindingStorageBufferUpdateAfterBind);
    merge(
        v12.descriptorBindingUniformTexelBufferUpdateAfterBind,
        di.descriptorBindingUniformTexelBufferUpdateAfterBind
    );
    merge(
        v12.descriptorBindingStorageTexelBufferUpdateAfterBind,
        di.descriptorBindingStorageTexelBufferUpdateAfterBind
    );
    merge(v12.descriptorBindingUpdateUnusedWhilePending, di.descriptorBindingUpdateUnusedWhilePending);
    merge(v12.descriptorBindingPartiallyBound, di.descriptorBindingPartiallyBound);
    merge(v12.descriptorBindingVariableDescriptorCount, di.descriptorBindingVariableDescriptorCount);
    merge(v12.runtimeDescriptorArray, di.runtimeDescriptorArray);
    // Structures of promoted extensions that are not part of VulkanExtendedFeatures.
    // Only the features that slang-rhi checks in vulkan12Features are merged.
    if (auto s = findInChain<VkPhysicalDeviceShaderFloat16Int8Features>(
            chain,
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES
        ))
    {
        v12.shaderFloat16 |= s->shaderFloat16;
        v12.shaderInt8 |= s->shaderInt8;
    }
    if (auto s = findInChain<VkPhysicalDeviceShaderAtomicInt64Features>(
            chain,
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES
        ))
    {
        v12.shaderBufferInt64Atomics |= s->shaderBufferInt64Atomics;
        v12.shaderSharedInt64Atomics |= s->shaderSharedInt64Atomics;
    }
    if (auto s = findInChain<VkPhysicalDeviceBufferDeviceAddressFeatures>(
            chain,
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES
        ))
    {
        v12.bufferDeviceAddress |= s->bufferDeviceAddress;
        v12.bufferDeviceAddressCaptureReplay |= s->bufferDeviceAddressCaptureReplay;
        v12.bufferDeviceAddressMultiDevice |= s->bufferDeviceAddressMultiDevice;
    }
    if (auto s = findInChain<VkPhysicalDeviceVulkanMemoryModelFeatures>(
            chain,
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES
        ))
    {
        v12.vulkanMemoryModel |= s->vulkanMemoryModel;
        v12.vulkanMemoryModelDeviceScope |= s->vulkanMemoryModelDeviceScope;
        v12.vulkanMemoryModelAvailabilityVisibilityChains |= s->vulkanMemoryModelAvailabilityVisibilityChains;
    }
    if (auto s = findInChain<VkPhysicalDeviceTimelineSemaphoreFeatures>(
            chain,
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES
        ))
    {
        v12.timelineSemaphore |= s->timelineSemaphore;
    }
    // Vulkan 1.2 features that correspond to enabling an extension.
    if (enabledExtensions.count(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME))
        v12.descriptorIndexing = VK_TRUE;
    if (enabledExtensions.count(VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME))
        v12.shaderOutputViewportIndex = v12.shaderOutputLayer = VK_TRUE;

    // Vulkan 1.3
    auto& v13 = e.vulkan13Features;
    merge(v13.inlineUniformBlock, e.inlineUniformBlockFeatures.inlineUniformBlock);
    merge(
        v13.descriptorBindingInlineUniformBlockUpdateAfterBind,
        e.inlineUniformBlockFeatures.descriptorBindingInlineUniformBlockUpdateAfterBind
    );
    merge(v13.dynamicRendering, e.dynamicRenderingFeatures.dynamicRendering);
    merge(v13.shaderIntegerDotProduct, e.shaderIntegerDotProductFeatures.shaderIntegerDotProduct);
    merge(v13.shaderDemoteToHelperInvocation, e.shaderDemoteToHelperInvocationFeatures.shaderDemoteToHelperInvocation);

    // Vulkan 1.4
    auto& v14 = e.vulkan14Features;
    merge(v14.dynamicRenderingLocalRead, e.dynamicRenderingLocalReadFeatures.dynamicRenderingLocalRead);
    merge(v14.shaderSubgroupRotate, e.shaderSubgroupRotateFeatures.shaderSubgroupRotate);
    merge(v14.shaderSubgroupRotateClustered, e.shaderSubgroupRotateFeatures.shaderSubgroupRotateClustered);

    return e;
}

// Links `s` into the pNext chain of `head`.
template<typename H, typename T>
static void linkStruct(H& head, T& s)
{
    s.pNext = head.pNext;
    head.pNext = &s;
}

// Clears the pNext pointers left behind by querying through a chain.
static void resetFeatureChain(VulkanExtendedFeatures& features)
{
#define RESET_CHAIN(name) features.name.pNext = nullptr;
    VK_EXTENDED_FEATURE_STRUCTS(RESET_CHAIN)
    RESET_CHAIN(vulkan12Features)
    RESET_CHAIN(vulkan13Features)
    RESET_CHAIN(vulkan14Features)
#undef RESET_CHAIN
}

void querySupportedFeatures(
    const VulkanApi& api,
    uint32_t apiVersion,
    VulkanFeatureSet& outSupported,
    VulkanDeviceProperties& outProperties
)
{
    outSupported = {};
    outProperties = {};
    outSupported.apiVersion = apiVersion;

    uint32_t extensionCount = 0;
    api.vkEnumerateDeviceExtensionProperties(api.m_physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    api.vkEnumerateDeviceExtensionProperties(api.m_physicalDevice, nullptr, &extensionCount, extensions.data());
    for (const auto& extension : extensions)
        outSupported.extensions.emplace(extension.extensionName);

    api.vkGetPhysicalDeviceFeatures(api.m_physicalDevice, &outSupported.features);

    if (apiVersion >= VK_API_VERSION_1_1 && api.vkGetPhysicalDeviceFeatures2 && api.vkGetPhysicalDeviceProperties2)
    {
        VulkanExtendedFeatures& e = outSupported.extendedFeatures;
        VkPhysicalDeviceFeatures2 features2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
#define LINK_FEATURES(name) linkStruct(features2, e.name);
        VK_EXTENDED_FEATURE_STRUCTS(LINK_FEATURES)
#undef LINK_FEATURES
        if (apiVersion >= VK_API_VERSION_1_2)
            linkStruct(features2, e.vulkan12Features);
        if (apiVersion >= VK_API_VERSION_1_3)
            linkStruct(features2, e.vulkan13Features);
        if (apiVersion >= VK_API_VERSION_1_4)
            linkStruct(features2, e.vulkan14Features);
        api.vkGetPhysicalDeviceFeatures2(api.m_physicalDevice, &features2);
        resetFeatureChain(e);

        VkPhysicalDeviceProperties2 properties2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        linkStruct(properties2, outProperties.rayTracingPipelineProperties);
        linkStruct(properties2, outProperties.subgroupProperties);
        api.vkGetPhysicalDeviceProperties2(api.m_physicalDevice, &properties2);
        outProperties.rayTracingPipelineProperties.pNext = nullptr;
        outProperties.subgroupProperties.pNext = nullptr;
    }

    if (api.vkGetPhysicalDeviceProperties2 &&
        (apiVersion >= VK_API_VERSION_1_2 || outSupported.hasExtension(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME)))
    {
        VkPhysicalDeviceDriverProperties driverProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceProperties2 properties2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        linkStruct(properties2, driverProperties);
        api.vkGetPhysicalDeviceProperties2(api.m_physicalDevice, &properties2);
        outProperties.driverID = driverProperties.driverID;
    }

    if (outSupported.hasExtension(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME) && api.vkGetPhysicalDeviceFeatures2)
    {
        VkPhysicalDeviceFeatures2 features2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        linkStruct(features2, outSupported.portabilitySubsetFeatures);
        api.vkGetPhysicalDeviceFeatures2(api.m_physicalDevice, &features2);
        outSupported.portabilitySubsetFeatures.pNext = nullptr;
    }
}

void restrictToEnabledFeatures(VulkanFeatureSet& features, const VkDeviceCreateInfo& createInfo)
{
    std::set<std::string> enabledExtensions;
    for (uint32_t i = 0; i < createInfo.enabledExtensionCount; i++)
    {
        const char* name = createInfo.ppEnabledExtensionNames[i];
        if (name && features.hasExtension(name))
            enabledExtensions.insert(name);
    }
    for (const auto& [name, version] : kPromotedDeviceExtensions)
    {
        if (features.hasVersion(version))
            enabledExtensions.insert(name);
    }
    features.extensions = std::move(enabledExtensions);

    VkPhysicalDeviceFeatures enabledFeatures;
    VulkanExtendedFeatures enabledExtendedFeatures =
        getEnabledFeatures(createInfo, features.extensions, enabledFeatures);

    restrictFeatureBits(features.features, enabledFeatures, 0);
#define RESTRICT_FEATURES(name) restrictFeatureBits(features.extendedFeatures.name, enabledExtendedFeatures.name);
    VK_EXTENDED_FEATURE_STRUCTS(RESTRICT_FEATURES)
    RESTRICT_FEATURES(vulkan12Features)
    RESTRICT_FEATURES(vulkan13Features)
    RESTRICT_FEATURES(vulkan14Features)
#undef RESTRICT_FEATURES

    VkPhysicalDevicePortabilitySubsetFeaturesKHR enabledPortabilitySubsetFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR
    };
    if (auto s = findInChain<VkPhysicalDevicePortabilitySubsetFeaturesKHR>(
            createInfo.pNext,
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR
        ))
        copyFeatureBits(enabledPortabilitySubsetFeatures, *s);
    restrictFeatureBits(features.portabilitySubsetFeatures, enabledPortabilitySubsetFeatures);
}

int findImportedQueueFamily(const VulkanApi& api, const VkDeviceCreateInfo& createInfo)
{
    uint32_t queueFamilyCount = 0;
    api.vkGetPhysicalDeviceQueueFamilyProperties(api.m_physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    api.vkGetPhysicalDeviceQueueFamilyProperties(api.m_physicalDevice, &queueFamilyCount, queueFamilies.data());
    const VkQueueFlags requiredFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    for (uint32_t i = 0; i < createInfo.queueCreateInfoCount; i++)
    {
        const VkDeviceQueueCreateInfo& queueCreateInfo = createInfo.pQueueCreateInfos[i];
        if (queueCreateInfo.flags == 0 && queueCreateInfo.queueCount > 0 &&
            queueCreateInfo.queueFamilyIndex < queueFamilyCount &&
            (queueFamilies[queueCreateInfo.queueFamilyIndex].queueFlags & requiredFlags) == requiredFlags)
        {
            return int(queueCreateInfo.queueFamilyIndex);
        }
    }
    return -1;
}

} // namespace rhi::vk
