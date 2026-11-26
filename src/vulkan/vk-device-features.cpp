#include "vk-device-features.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
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

// Returns true if any feature bit is set in a feature structure.
template<typename T>
static bool hasAnyFeatureBits(const T& features)
{
    const VkBool32* bits =
        reinterpret_cast<const VkBool32*>(reinterpret_cast<const uint8_t*>(&features) + kFeatureBitsOffset);
    for (size_t i = 0; i < (sizeof(T) - kFeatureBitsOffset) / sizeof(VkBool32); i++)
        if (bits[i])
            return true;
    return false;
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

namespace {

// Helper for selectDeviceFeatures: enables extensions and feature structures, if they are available.
class FeatureSelector
{
public:
    FeatureSelector(const VulkanFeatureSet& available, VulkanFeatureSet& enabled)
        : m_available(available)
        , m_enabled(enabled)
    {
    }

    bool isAvailable(const char* extension) const { return m_available.hasExtension(extension); }

    // Enables an extension if it is available.
    bool enableExtension(const char* extension)
    {
        if (!m_available.hasExtension(extension))
            return false;
        m_enabled.extensions.insert(extension);
        return true;
    }

    // Enables an extension together with all available features of one of its feature structures, if `condition`
    // holds and the extension is available.
    template<typename T>
    bool enableFeatures(T VulkanExtendedFeatures::* member, bool condition, const char* extension)
    {
        if (!condition || !enableExtension(extension))
            return false;
        copyFeatureBits(m_enabled.extendedFeatures.*member, m_available.extendedFeatures.*member);
        return true;
    }

private:
    const VulkanFeatureSet& m_available;
    VulkanFeatureSet& m_enabled;
};

} // namespace

void selectDeviceFeatures(const VulkanFeatureSet& available, const DeviceDesc& desc, VulkanFeatureSet& outEnabled)
{
    using F = VulkanExtendedFeatures;
    const VulkanExtendedFeatures& a = available.extendedFeatures;

    outEnabled = {};
    outEnabled.apiVersion = available.apiVersion;

    // Enable all available core features, and all available Vulkan 1.2 features. Features of Vulkan 1.1, 1.3 and 1.4
    // are enabled through the structures of the extensions that were promoted to these versions instead
    // (a pNext chain must not contain both).
    outEnabled.features = available.features;
    copyFeatureBits(outEnabled.extendedFeatures.vulkan12Features, a.vulkan12Features);

    FeatureSelector s(available, outEnabled);

    s.enableExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    s.enableExtension(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);

    if (s.enableExtension(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
    {
        outEnabled.portabilitySubsetFeatures.imageView2DOn3DImage =
            available.portabilitySubsetFeatures.imageView2DOn3DImage;
    }

    // VK_KHR_shader_abort lets a shader call abort() (lowered by Slang to OpAbortKHR). Executing
    // it ceases the invocation and loses the device; the abort message is then retrievable only
    // via VK_KHR_device_fault (vkGetDeviceFaultDebugInfoKHR + VkDeviceFaultShaderAbortMessageInfoKHR).
    // VK_KHR_shader_abort hard-depends on both VK_KHR_device_fault and VK_KHR_shader_constant_data,
    // so the three must be enabled together. Feature::ShaderAbort is reported only when the whole
    // round-trip (abort + fault-message retrieval) is available, since that is what the dependent
    // runtime test (shader-slang/slang#11790) needs.
    if (s.isAvailable(VK_KHR_SHADER_ABORT_EXTENSION_NAME) && s.isAvailable(VK_KHR_DEVICE_FAULT_EXTENSION_NAME) &&
        s.isAvailable(VK_KHR_SHADER_CONSTANT_DATA_EXTENSION_NAME) && a.shaderAbortFeatures.shaderAbort &&
        a.faultFeatures.deviceFault && a.shaderConstantDataFeatures.shaderConstantData)
    {
        s.enableFeatures(&F::shaderConstantDataFeatures, true, VK_KHR_SHADER_CONSTANT_DATA_EXTENSION_NAME);
        s.enableFeatures(&F::faultFeatures, true, VK_KHR_DEVICE_FAULT_EXTENSION_NAME);
        s.enableFeatures(&F::shaderAbortFeatures, true, VK_KHR_SHADER_ABORT_EXTENSION_NAME);
    }

    s.enableFeatures(
        &F::shaderDrawParametersFeatures,
        a.shaderDrawParametersFeatures.shaderDrawParameters,
        VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME
    );
    s.enableFeatures(
        &F::dynamicRenderingFeatures,
        a.dynamicRenderingFeatures.dynamicRendering,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
    );
    s.enableFeatures(
        &F::dynamicRenderingLocalReadFeatures,
        a.dynamicRenderingLocalReadFeatures.dynamicRenderingLocalRead,
        VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME
    );
    s.enableFeatures(&F::formats4444Features, a.formats4444Features.formatA4R4G4B4, VK_EXT_4444_FORMATS_EXTENSION_NAME);
    s.enableFeatures(
        &F::storage16BitFeatures,
        a.storage16BitFeatures.storageBuffer16BitAccess,
        VK_KHR_16BIT_STORAGE_EXTENSION_NAME
    );

    // Enable each atomic extension when any of its features is supported. In particular, float32
    // min/max does not depend on float16 atomics.
    s.enableFeatures(
        &F::atomicFloatFeatures,
        hasAnyFeatureBits(a.atomicFloatFeatures),
        VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME
    );
    if (s.isAvailable(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME) && s.enableFeatures(
                                                                        &F::atomicFloat2Features,
                                                                        hasAnyFeatureBits(a.atomicFloat2Features),
                                                                        VK_EXT_SHADER_ATOMIC_FLOAT_2_EXTENSION_NAME
                                                                    ))
    {
        // VK_EXT_shader_atomic_float2 depends on VK_EXT_shader_atomic_float even if none of
        // the latter's optional feature bits is supported.
        s.enableExtension(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
    }

    s.enableFeatures(
        &F::imageInt64AtomicFeatures,
        a.imageInt64AtomicFeatures.shaderImageInt64Atomics,
        VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME
    );
    s.enableFeatures(
        &F::extendedDynamicStateFeatures,
        a.extendedDynamicStateFeatures.extendedDynamicState,
        VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME
    );
    s.enableFeatures(
        &F::customBorderColorFeatures,
        a.customBorderColorFeatures.customBorderColors,
        VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME
    );
    // Enable vertex attribute robustness to avoid validation errors.
    s.enableFeatures(
        &F::vertexAttributeRobustnessFeatures,
        a.vertexAttributeRobustnessFeatures.vertexAttributeRobustness,
        VK_EXT_VERTEX_ATTRIBUTE_ROBUSTNESS_EXTENSION_NAME
    );
    // Instances currently target at most Vulkan 1.3, so rotation still requires the KHR
    // extension even when the physical device supports Vulkan 1.4.
    s.enableFeatures(
        &F::shaderSubgroupRotateFeatures,
        a.shaderSubgroupRotateFeatures.shaderSubgroupRotate,
        VK_KHR_SHADER_SUBGROUP_ROTATE_EXTENSION_NAME
    );

    // See DeviceDesc::enableRayTracing for rationale.
    if (desc.enableRayTracing && s.isAvailable(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) &&
        s.enableFeatures(
            &F::accelerationStructureFeatures,
            a.accelerationStructureFeatures.accelerationStructure,
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME
        ))
    {
        s.enableExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

        // The following all depend on VK_KHR_acceleration_structure.
        if (available.hasVersion(VK_API_VERSION_1_3) || s.isAvailable(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME))
        {
            if (s.enableFeatures(
                    &F::opacityMicromapFeatures,
                    a.opacityMicromapFeatures.micromap,
                    VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME
                ) &&
                !available.hasVersion(VK_API_VERSION_1_3))
            {
                s.enableExtension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
            }
        }
        s.enableFeatures(
            &F::rayTracingPipelineFeatures,
            a.rayTracingPipelineFeatures.rayTracingPipeline,
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME
        );
        s.enableFeatures(
            &F::rayTracingPositionFetchFeatures,
            a.rayTracingPositionFetchFeatures.rayTracingPositionFetch,
            VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME
        );
        s.enableFeatures(&F::rayQueryFeatures, a.rayQueryFeatures.rayQuery, VK_KHR_RAY_QUERY_EXTENSION_NAME);
        s.enableFeatures(
            &F::rayTracingLinearSweptSpheresFeatures,
            a.rayTracingLinearSweptSpheresFeatures.spheres || a.rayTracingLinearSweptSpheresFeatures.linearSweptSpheres,
            VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME
        );
        s.enableFeatures(
            &F::clusterAccelerationStructureFeatures,
            a.clusterAccelerationStructureFeatures.clusterAccelerationStructure,
            VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME
        );
    }

    s.enableFeatures(
        &F::inlineUniformBlockFeatures,
        a.inlineUniformBlockFeatures.inlineUniformBlock,
        VK_EXT_INLINE_UNIFORM_BLOCK_EXTENSION_NAME
    );
    s.enableFeatures(&F::robustness2Features, a.robustness2Features.nullDescriptor, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
    s.enableFeatures(&F::clockFeatures, a.clockFeatures.shaderDeviceClock, VK_KHR_SHADER_CLOCK_EXTENSION_NAME);
    s.enableFeatures(&F::meshShaderFeatures, a.meshShaderFeatures.meshShader, VK_EXT_MESH_SHADER_EXTENSION_NAME);
    s.enableFeatures(&F::multiviewFeatures, a.multiviewFeatures.multiview, VK_KHR_MULTIVIEW_EXTENSION_NAME);
    s.enableFeatures(
        &F::fragmentShadingRateFeatures,
        a.fragmentShadingRateFeatures.primitiveFragmentShadingRate,
        VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME
    );
    s.enableFeatures(
        &F::rayTracingInvocationReorderFeatures,
        a.rayTracingInvocationReorderFeatures.rayTracingInvocationReorder,
        VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME
    );
    s.enableFeatures(
        &F::variablePointersFeatures,
        a.variablePointersFeatures.variablePointers,
        VK_KHR_VARIABLE_POINTERS_EXTENSION_NAME
    );
    s.enableFeatures(
        &F::computeShaderDerivativesFeatures,
        a.computeShaderDerivativesFeatures.computeDerivativeGroupQuads,
        VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME
    );
    // Also enable VK_NV_compute_shader_derivatives even if VK_KHR_compute_shader_derivatives is available.
    // This avoids warnings from the validation layer if the NV specific intrinsics are used.
    s.enableExtension(VK_NV_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);

    if (desc.enableRayTracingValidation)
    {
        s.enableFeatures(
            &F::rayTracingValidationFeatures,
            a.rayTracingValidationFeatures.rayTracingValidation,
            VK_NV_RAY_TRACING_VALIDATION_EXTENSION_NAME
        );
    }

    s.enableFeatures(
        &F::shaderMaximalReconvergenceFeatures,
        a.shaderMaximalReconvergenceFeatures.shaderMaximalReconvergence,
        VK_KHR_SHADER_MAXIMAL_RECONVERGENCE_EXTENSION_NAME
    );
    s.enableFeatures(
        &F::shaderQuadControlFeatures,
        a.shaderQuadControlFeatures.shaderQuadControl,
        VK_KHR_SHADER_QUAD_CONTROL_EXTENSION_NAME
    );
    s.enableFeatures(
        &F::shaderIntegerDotProductFeatures,
        a.shaderIntegerDotProductFeatures.shaderIntegerDotProduct,
        VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME
    );
    s.enableFeatures(
        &F::cooperativeVectorFeatures,
        a.cooperativeVectorFeatures.cooperativeVector,
        VK_NV_COOPERATIVE_VECTOR_EXTENSION_NAME
    );
    s.enableFeatures(
        &F::cooperativeMatrix1Features,
        a.cooperativeMatrix1Features.cooperativeMatrix,
        VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME
    );
    // VK_EXT_shader_float8 is required for cooperative matrix types FloatE4M3 and FloatE5M2.
    s.enableFeatures(
        &F::shaderFloat8Features,
        a.shaderFloat8Features.shaderFloat8,
        VK_EXT_SHADER_FLOAT8_EXTENSION_NAME
    );
    s.enableFeatures(
        &F::shaderBfloat16Features,
        a.shaderBfloat16Features.shaderBFloat16Type,
        VK_KHR_SHADER_BFLOAT16_EXTENSION_NAME
    );

    // VK_NV_cooperative_matrix2 has independent feature bits - one does not imply another - so the extension is
    // enabled whenever any bit that slang-rhi exposes is supported (flexible dimensions has no SPIR-V capability and
    // is not exposed). It depends on VK_KHR_cooperative_matrix (its capabilities require SPV_KHR_cooperative_matrix),
    // so it is only enabled together with it.
    const auto& coopMat2 = a.cooperativeMatrix2Features;
    if (outEnabled.extendedFeatures.cooperativeMatrix1Features.cooperativeMatrix)
    {
        s.enableFeatures(
            &F::cooperativeMatrix2Features,
            coopMat2.cooperativeMatrixWorkgroupScope || coopMat2.cooperativeMatrixReductions ||
                coopMat2.cooperativeMatrixConversions || coopMat2.cooperativeMatrixPerElementOperations ||
                coopMat2.cooperativeMatrixTensorAddressing || coopMat2.cooperativeMatrixBlockLoads,
            VK_NV_COOPERATIVE_MATRIX_2_EXTENSION_NAME
        );
    }

    s.enableFeatures(
        &F::mutableDescriptorTypeFeatures,
        a.mutableDescriptorTypeFeatures.mutableDescriptorType,
        VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME
    );
    if (s.enableFeatures(
            &F::pipelineBinaryFeatures,
            a.pipelineBinaryFeatures.pipelineBinaries,
            VK_KHR_PIPELINE_BINARY_EXTENSION_NAME
        ))
    {
        s.enableExtension(VK_KHR_MAINTENANCE_5_EXTENSION_NAME);
    }
    s.enableFeatures(
        &F::shaderReplicatedCompositesFeatures,
        a.shaderReplicatedCompositesFeatures.shaderReplicatedComposites,
        VK_EXT_SHADER_REPLICATED_COMPOSITES_EXTENSION_NAME
    );
    s.enableFeatures(
        &F::fragmentShaderBarycentricFeatures,
        a.fragmentShaderBarycentricFeatures.fragmentShaderBarycentric,
        VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME
    );
    s.enableFeatures(
        &F::shaderDemoteToHelperInvocationFeatures,
        a.shaderDemoteToHelperInvocationFeatures.shaderDemoteToHelperInvocation,
        VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME
    );
    s.enableFeatures(
        &F::rayTracingMotionBlurFeatures,
        a.rayTracingMotionBlurFeatures.rayTracingMotionBlur,
        VK_NV_RAY_TRACING_MOTION_BLUR_EXTENSION_NAME
    );
    // The EXT version needs both the extension and the shaderSubgroupPartitioned feature. The NV version reports its
    // support through a subgroup property instead of a feature.
    s.enableFeatures(
        &F::shaderSubgroupPartitionedFeatures,
        a.shaderSubgroupPartitionedFeatures.shaderSubgroupPartitioned,
        VK_EXT_SHADER_SUBGROUP_PARTITIONED_EXTENSION_NAME
    );
    s.enableExtension(VK_NV_SHADER_SUBGROUP_PARTITIONED_EXTENSION_NAME);

    s.enableFeatures(&F::fragmentShaderInterlockFeatures, true, VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME);

    // Extensions without features.
    if (s.enableExtension(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME))
    {
#if SLANG_WINDOWS_FAMILY
        s.enableExtension(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
#else
        s.enableExtension(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
#endif
    }
    if (s.enableExtension(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME))
    {
#if SLANG_WINDOWS_FAMILY
        s.enableExtension(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
#else
        s.enableExtension(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
#endif
    }
    s.enableExtension(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);
    s.enableExtension(VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME);
    // See DeviceDesc::enableCUDALaunchFromGfx for rationale.
    if (desc.enableCUDALaunchFromGfx)
        s.enableExtension(VK_NVX_BINARY_IMPORT_EXTENSION_NAME);
    s.enableExtension(VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME);
    s.enableExtension(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
}

void linkEnabledFeatures(VulkanFeatureSet& enabled, VkDeviceCreateInfo& createInfo)
{
    createInfo.pEnabledFeatures = &enabled.features;

    // selectDeviceFeatures() only enables the features of a structure together with its extension, so every
    // structure with enabled features can be linked.
#define LINK_FEATURES(name)                                                                                            \
    if (hasAnyFeatureBits(enabled.extendedFeatures.name))                                                              \
        linkStruct(createInfo, enabled.extendedFeatures.name);
    VK_EXTENDED_FEATURE_STRUCTS(LINK_FEATURES)
    LINK_FEATURES(vulkan12Features)
#undef LINK_FEATURES

    if (enabled.hasExtension(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
        linkStruct(createInfo, enabled.portabilitySubsetFeatures);
}

void reportDeviceFeatures(
    const VulkanFeatureSet& enabled,
    const VulkanDeviceProperties& properties,
    std::vector<Feature>& outFeatures,
    std::vector<Capability>& outCapabilities
)
{
    const VkPhysicalDeviceFeatures& f = enabled.features;
    const VulkanExtendedFeatures& e = enabled.extendedFeatures;
    const VkPhysicalDeviceVulkan12Features& v12 = e.vulkan12Features;

    const auto add =
        [&](bool condition, std::initializer_list<Feature> features, std::initializer_list<Capability> capabilities)
    {
        if (!condition)
            return;
        outFeatures.insert(outFeatures.end(), features);
        outCapabilities.insert(outCapabilities.end(), capabilities);
    };
    const auto addCapabilities = [&](bool condition, std::initializer_list<Capability> capabilities)
    {
        add(condition, {}, capabilities);
    };

    add(enabled.hasExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME), {Feature::Surface}, {});
    addCapabilities(
        enabled.hasExtension(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME),
        {Capability::SPV_KHR_non_semantic_info, Capability::spvNonSemanticInfoKHR}
    );

    // Core features.
    add(f.shaderResourceMinLod, {Feature::ShaderResourceMinLod}, {Capability::spvMinLod});
    add(f.shaderFloat64, {Feature::Double}, {});
    add(f.shaderInt64, {Feature::Int64}, {});
    add(f.shaderInt16, {Feature::Int16}, {});

    // Vulkan 1.2 features.
    add(v12.shaderFloat16, {Feature::Half}, {});
    add(v12.shaderBufferInt64Atomics, {Feature::AtomicInt64}, {Capability::spvInt64Atomics});
    addCapabilities(
        v12.bufferDeviceAddress,
        {
            Capability::SPV_EXT_physical_storage_buffer,
            Capability::SPV_KHR_physical_storage_buffer,
            Capability::spvPhysicalStorageBufferEXT,
            Capability::spvPhysicalStorageBufferKHR,
        }
    );
    addCapabilities(
        v12.vulkanMemoryModel,
        {Capability::SPV_KHR_vulkan_memory_model, Capability::spvVulkanMemoryModelKHR}
    );
    addCapabilities(
        v12.vulkanMemoryModel && v12.vulkanMemoryModelDeviceScope,
        {Capability::spvVulkanMemoryModelDeviceScopeKHR}
    );
    addCapabilities(v12.descriptorIndexing, {Capability::SPV_EXT_descriptor_indexing});
    add(v12.descriptorIndexing && v12.shaderSampledImageArrayNonUniformIndexing &&
            v12.shaderStorageBufferArrayNonUniformIndexing && v12.shaderStorageImageArrayNonUniformIndexing &&
            v12.shaderUniformTexelBufferArrayNonUniformIndexing &&
            v12.shaderStorageTexelBufferArrayNonUniformIndexing && v12.descriptorBindingSampledImageUpdateAfterBind &&
            v12.descriptorBindingStorageImageUpdateAfterBind && v12.descriptorBindingStorageBufferUpdateAfterBind &&
            v12.descriptorBindingUniformTexelBufferUpdateAfterBind &&
            v12.descriptorBindingStorageTexelBufferUpdateAfterBind && v12.descriptorBindingUpdateUnusedWhilePending &&
            v12.descriptorBindingPartiallyBound && e.mutableDescriptorTypeFeatures.mutableDescriptorType,
        {Feature::Bindless},
        {});

    add(e.shaderAbortFeatures.shaderAbort, {Feature::ShaderAbort}, {Capability::SPV_KHR_abort, Capability::spvAbort});

    // Report shader capabilities for buffer atomics; shared and image atomics still require their own
    // storage-class-specific feature checks.
    const auto& atomicFloat = e.atomicFloatFeatures;
    add(atomicFloat.shaderBufferFloat32Atomics, {Feature::AtomicFloat}, {});
    addCapabilities(
        atomicFloat.shaderBufferFloat32AtomicAdd,
        {Capability::SPV_EXT_shader_atomic_float_add, Capability::spvAtomicFloat32AddEXT}
    );
    addCapabilities(
        atomicFloat.shaderBufferFloat64AtomicAdd,
        {Capability::SPV_EXT_shader_atomic_float_add, Capability::spvAtomicFloat64AddEXT}
    );
    const auto& atomicFloat2 = e.atomicFloat2Features;
    add(atomicFloat2.shaderBufferFloat16Atomics, {Feature::AtomicFloat}, {});
    addCapabilities(
        atomicFloat2.shaderBufferFloat16AtomicAdd,
        {Capability::SPV_EXT_shader_atomic_float16_add, Capability::spvAtomicFloat16AddEXT}
    );
    addCapabilities(
        atomicFloat2.shaderBufferFloat16AtomicMinMax,
        {Capability::SPV_EXT_shader_atomic_float_min_max, Capability::spvAtomicFloat16MinMaxEXT}
    );
    addCapabilities(
        atomicFloat2.shaderBufferFloat32AtomicMinMax,
        {Capability::SPV_EXT_shader_atomic_float_min_max, Capability::spvAtomicFloat32MinMaxEXT}
    );
    addCapabilities(
        atomicFloat2.shaderBufferFloat64AtomicMinMax,
        {Capability::SPV_EXT_shader_atomic_float_min_max, Capability::spvAtomicFloat64MinMaxEXT}
    );

    add(e.extendedDynamicStateFeatures.extendedDynamicState && f.multiViewport &&
            (v12.shaderOutputViewportIndex || enabled.hasExtension(VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME)),
        {Feature::ViewportArrayIndex},
        {});
    add(e.customBorderColorFeatures.customBorderColors, {Feature::CustomBorderColor}, {});
    addCapabilities(
        e.shaderSubgroupRotateFeatures.shaderSubgroupRotate,
        {
            Capability::SPV_KHR_subgroup_rotate,
            Capability::spvSubgroupRotateKHR,
            Capability::spvGroupNonUniformRotateKHR,
        }
    );

    // Ray tracing.
    add(e.accelerationStructureFeatures.accelerationStructure, {Feature::AccelerationStructure}, {});
    add(e.opacityMicromapFeatures.micromap, {Feature::OpacityMicromap}, {});
    add(e.rayTracingPipelineFeatures.rayTracingPipeline,
        {Feature::RayTracing},
        {Capability::SPV_KHR_ray_tracing, Capability::spvRayTracingKHR});
    addCapabilities(
        e.rayTracingPositionFetchFeatures.rayTracingPositionFetch,
        {Capability::SPV_KHR_ray_tracing_position_fetch, Capability::spvRayTracingPositionFetchKHR}
    );
    addCapabilities(
        e.rayTracingPositionFetchFeatures.rayTracingPositionFetch && e.rayQueryFeatures.rayQuery,
        {Capability::spvRayQueryPositionFetchKHR}
    );
    add(e.rayQueryFeatures.rayQuery, {Feature::RayQuery}, {Capability::SPV_KHR_ray_query, Capability::spvRayQueryKHR});
    add(e.rayTracingLinearSweptSpheresFeatures.spheres,
        {Feature::AccelerationStructureSpheres},
        {Capability::SPV_NV_linear_swept_spheres, Capability::spvRayTracingSpheresGeometryNV});
    add(e.rayTracingLinearSweptSpheresFeatures.linearSweptSpheres,
        {Feature::AccelerationStructureLinearSweptSpheres},
        {Capability::SPV_NV_linear_swept_spheres, Capability::spvRayTracingLinearSweptSpheresGeometryNV});
    add(e.clusterAccelerationStructureFeatures.clusterAccelerationStructure,
        {Feature::ClusterAccelerationStructure},
        {Capability::SPV_NV_cluster_acceleration_structure, Capability::spvRayTracingClusterAccelerationStructureNV});
    add(e.rayTracingInvocationReorderFeatures.rayTracingInvocationReorder,
        {Feature::ShaderExecutionReordering},
        {
            Capability::SPV_NV_shader_invocation_reorder,
            Capability::spvShaderInvocationReorderNV,
            Capability::spirv_nv,
        });
    add(e.rayTracingMotionBlurFeatures.rayTracingMotionBlur,
        {Feature::RayTracingMotionBlur},
        {Capability::SPV_NV_ray_tracing_motion_blur, Capability::spvRayTracingMotionBlurNV});
    add(e.rayTracingValidationFeatures.rayTracingValidation, {Feature::RayTracingValidation}, {});

    add(e.clockFeatures.shaderDeviceClock,
        {Feature::RealtimeClock},
        {Capability::SPV_KHR_shader_clock, Capability::spvShaderClockKHR});
    add(e.meshShaderFeatures.meshShader,
        {Feature::MeshShader},
        {Capability::SPV_EXT_mesh_shader, Capability::spvMeshShadingEXT});
    add(e.multiviewFeatures.multiview, {Feature::MultiView}, {});
    add(e.fragmentShadingRateFeatures.primitiveFragmentShadingRate, {Feature::FragmentShadingRate}, {});
    add(e.fragmentShaderBarycentricFeatures.fragmentShaderBarycentric,
        {Feature::Barycentrics},
        {Capability::SPV_KHR_fragment_shader_barycentric, Capability::spvFragmentBarycentricKHR});
    addCapabilities(
        e.variablePointersFeatures.variablePointers,
        {Capability::SPV_KHR_variable_pointers, Capability::spvVariablePointersKHR}
    );
    addCapabilities(
        e.computeShaderDerivativesFeatures.computeDerivativeGroupQuads,
        {Capability::SPV_KHR_compute_shader_derivatives, Capability::spvComputeShaderDerivativesKHR}
    );
    addCapabilities(
        e.shaderMaximalReconvergenceFeatures.shaderMaximalReconvergence,
        {Capability::SPV_KHR_maximal_reconvergence, Capability::spvMaximalReconvergenceKHR}
    );
    addCapabilities(
        e.shaderQuadControlFeatures.shaderQuadControl,
        {Capability::SPV_KHR_quad_control, Capability::spvQuadControlKHR}
    );
    addCapabilities(
        e.shaderReplicatedCompositesFeatures.shaderReplicatedComposites,
        {Capability::SPV_EXT_replicated_composites, Capability::spvReplicatedCompositesEXT}
    );
    addCapabilities(
        e.shaderDemoteToHelperInvocationFeatures.shaderDemoteToHelperInvocation,
        {
            Capability::SPV_EXT_demote_to_helper_invocation,
            Capability::spvDemoteToHelperInvocationEXT,
            Capability::spvDemoteToHelperInvocation,
        }
    );
    add(e.shaderSubgroupPartitionedFeatures.shaderSubgroupPartitioned, {Feature::SubgroupPartitionedEXT}, {});

    // Cooperative vectors and matrices.
    add(e.cooperativeVectorFeatures.cooperativeVector,
        {Feature::CooperativeVector},
        {Capability::SPV_NV_cooperative_vector, Capability::spvCooperativeVectorNV});
    addCapabilities(
        e.cooperativeVectorFeatures.cooperativeVector && e.cooperativeVectorFeatures.cooperativeVectorTraining,
        {Capability::spvCooperativeVectorTrainingNV}
    );
    add(e.cooperativeMatrix1Features.cooperativeMatrix,
        {Feature::CooperativeMatrix},
        {Capability::SPV_KHR_cooperative_matrix, Capability::spvCooperativeMatrixKHR});
    add(e.shaderFloat8Features.shaderFloat8, {Feature::Float8}, {Capability::SPV_EXT_float8, Capability::spvFloat8EXT});
    add(e.shaderBfloat16Features.shaderBFloat16Type,
        {Feature::Bfloat16},
        {Capability::SPV_KHR_bfloat16, Capability::spvBFloat16KHR});
    const auto& coopMat2 = e.cooperativeMatrix2Features;
    add(coopMat2.cooperativeMatrixWorkgroupScope,
        {Feature::CooperativeMatrix2},
        {Capability::SPV_NV_cooperative_matrix2});
    add(coopMat2.cooperativeMatrixReductions,
        {Feature::CooperativeMatrixReductions},
        {Capability::spvCooperativeMatrixReductionsNV});
    add(coopMat2.cooperativeMatrixConversions,
        {Feature::CooperativeMatrixConversions},
        {Capability::spvCooperativeMatrixConversionsNV});
    add(coopMat2.cooperativeMatrixPerElementOperations,
        {Feature::CooperativeMatrixPerElementOperations},
        {Capability::spvCooperativeMatrixPerElementOperationsNV});
    // This bit enables both the CooperativeMatrixTensorAddressingNV and the standalone TensorAddressingNV SPIR-V
    // capability (TensorView/TensorLayout), so advertise both.
    add(coopMat2.cooperativeMatrixTensorAddressing,
        {Feature::CooperativeMatrixTensorAddressing},
        {Capability::spvCooperativeMatrixTensorAddressingNV, Capability::spvTensorAddressingNV});
    add(coopMat2.cooperativeMatrixBlockLoads,
        {Feature::CooperativeMatrixBlockLoads},
        {Capability::spvCooperativeMatrixBlockLoadsNV});

    // Extensions without features.
    add(enabled.hasExtension(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME),
        {Feature::ConservativeRasterization},
        {Capability::SPV_EXT_fragment_fully_covered, Capability::spvFragmentFullyCoveredEXT});
    // TODO: Should Feature::RasterizerOrderedViews be reported?
    addCapabilities(
        enabled.hasExtension(VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME),
        {Capability::SPV_EXT_fragment_shader_interlock}
    );
    addCapabilities(
        e.fragmentShaderInterlockFeatures.fragmentShaderPixelInterlock,
        {Capability::spvFragmentShaderPixelInterlockEXT}
    );

    // Subgroup operations. Report subgroup capabilities independently of the approximate WaveOps feature.
    const VkSubgroupFeatureFlags subgroupOperations = properties.subgroupProperties.supportedOperations;
    static constexpr std::pair<VkSubgroupFeatureFlagBits, Capability> subgroupCapabilities[] = {
        {VK_SUBGROUP_FEATURE_BASIC_BIT, Capability::spvGroupNonUniform},
        {VK_SUBGROUP_FEATURE_VOTE_BIT, Capability::spvGroupNonUniformVote},
        {VK_SUBGROUP_FEATURE_ARITHMETIC_BIT, Capability::spvGroupNonUniformArithmetic},
        {VK_SUBGROUP_FEATURE_BALLOT_BIT, Capability::spvGroupNonUniformBallot},
        {VK_SUBGROUP_FEATURE_SHUFFLE_BIT, Capability::spvGroupNonUniformShuffle},
        {VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT, Capability::spvGroupNonUniformShuffleRelative},
        {VK_SUBGROUP_FEATURE_CLUSTERED_BIT, Capability::spvGroupNonUniformClustered},
        {VK_SUBGROUP_FEATURE_QUAD_BIT, Capability::spvGroupNonUniformQuad},
    };
    for (const auto& [operation, capability] : subgroupCapabilities)
        addCapabilities(subgroupOperations & operation, {capability});
    // Approximate DX12's WaveOps boolean.
    add(subgroupOperations & kWaveOpsSubgroupFeatureMask, {Feature::WaveOps}, {});
    addCapabilities(
        enabled.hasExtension(VK_NV_SHADER_SUBGROUP_PARTITIONED_EXTENSION_NAME) &&
            (subgroupOperations & VK_SUBGROUP_FEATURE_PARTITIONED_BIT_NV),
        {Capability::SPV_NV_shader_subgroup_partitioned, Capability::spvGroupNonUniformPartitionedNV}
    );

    // SPIR-V versions.
    addCapabilities(true, {Capability::_spirv_1_0});
    addCapabilities(
        enabled.hasVersion(VK_API_VERSION_1_1),
        {Capability::_spirv_1_1, Capability::_spirv_1_2, Capability::_spirv_1_3}
    );
    addCapabilities(enabled.hasVersion(VK_API_VERSION_1_2), {Capability::_spirv_1_4, Capability::_spirv_1_5});
    addCapabilities(enabled.hasVersion(VK_API_VERSION_1_3), {Capability::_spirv_1_6});

#if SLANG_PROCESSOR_ARM_64
    // Ray tracing pipelines are currently broken on ARM64 lavapipe. Keep feature detection and device
    // creation unchanged, but do not advertise the affected features or shader capabilities.
    if (properties.driverID == VK_DRIVER_ID_MESA_LLVMPIPE)
    {
        auto remove = [](auto& values, auto value)
        {
            values.erase(std::remove(values.begin(), values.end(), value), values.end());
        };

        for (Feature feature : {
                 Feature::AccelerationStructure,
                 Feature::AccelerationStructureSpheres,
                 Feature::AccelerationStructureLinearSweptSpheres,
                 Feature::RayTracing,
                 Feature::RayQuery,
                 Feature::ShaderExecutionReordering,
                 Feature::RayTracingMotionBlur,
                 Feature::RayTracingValidation,
                 Feature::ClusterAccelerationStructure,
             })
        {
            remove(outFeatures, feature);
        }

        for (Capability capability : {
                 Capability::SPV_KHR_ray_tracing,
                 Capability::SPV_KHR_ray_query,
                 Capability::SPV_KHR_ray_tracing_position_fetch,
                 Capability::SPV_NV_ray_tracing_motion_blur,
                 Capability::SPV_NV_shader_invocation_reorder,
                 Capability::SPV_NV_cluster_acceleration_structure,
                 Capability::SPV_NV_linear_swept_spheres,
                 Capability::spvRayTracingMotionBlurNV,
                 Capability::spvRayTracingKHR,
                 Capability::spvRayTracingPositionFetchKHR,
                 Capability::spvRayQueryKHR,
                 Capability::spvRayQueryPositionFetchKHR,
                 Capability::spvShaderInvocationReorderNV,
                 Capability::spirv_nv,
                 Capability::spvRayTracingClusterAccelerationStructureNV,
                 Capability::spvRayTracingLinearSweptSpheresGeometryNV,
                 Capability::spvRayTracingSpheresGeometryNV,
             })
        {
            remove(outCapabilities, capability);
        }
    }
#endif
}

} // namespace rhi::vk
