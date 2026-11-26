#pragma once

#include "vk-api.h"

#include <set>
#include <string>
#include <vector>

namespace rhi::vk {

/// Subgroup operations that approximate D3D12's wave operations.
static constexpr VkSubgroupFeatureFlags kWaveOpsSubgroupFeatureMask =
    VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT | VK_SUBGROUP_FEATURE_ARITHMETIC_BIT |
    VK_SUBGROUP_FEATURE_BALLOT_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT |
    VK_SUBGROUP_FEATURE_CLUSTERED_BIT | VK_SUBGROUP_FEATURE_QUAD_BIT | VK_SUBGROUP_FEATURE_PARTITIONED_BIT_NV;

/// Links the structure `s` into the pNext chain of `head`.
template<typename H, typename T>
inline void linkStruct(H& head, T& s)
{
    s.pNext = const_cast<void*>(head.pNext);
    head.pNext = &s;
}

/// A set of Vulkan extensions and features.
/// Describes what a physical device supports, what an imported device was created with, or what slang-rhi enables.
struct VulkanFeatureSet
{
    /// API version (major and minor only).
    uint32_t apiVersion = 0;
    std::set<std::string> extensions;
    VkPhysicalDeviceFeatures features = {};
    VulkanExtendedFeatures extendedFeatures;
    VkPhysicalDevicePortabilitySubsetFeaturesKHR portabilitySubsetFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR
    };

    bool hasVersion(uint32_t version) const { return apiVersion >= version; }
    bool hasExtension(const char* name) const { return extensions.count(name) != 0; }
};

/// Physical device properties that feature detection depends on.
struct VulkanDeviceProperties
{
    VkPhysicalDeviceSubgroupProperties subgroupProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR
    };
    VkDriverId driverID = VkDriverId(0);
};

/// Queries the extensions and features that the physical device of `api` supports when used with `apiVersion`.
void querySupportedFeatures(
    const VulkanApi& api,
    uint32_t apiVersion,
    VulkanFeatureSet& outSupported,
    VulkanDeviceProperties& outProperties
);

/// Restricts `features` to the extensions and features enabled by `createInfo`, the create info of an imported
/// device. Features that are part of a core version and of an extension promoted to it are recognized regardless of
/// which structure was used to enable them.
void restrictToEnabledFeatures(VulkanFeatureSet& features, const VkDeviceCreateInfo& createInfo);

/// Returns the first queue family in the create info of an imported device that supports graphics and compute,
/// or -1 if there is none.
int findImportedQueueFamily(const VulkanApi& api, const VkDeviceCreateInfo& createInfo);

/// Selects the extensions and features that slang-rhi uses, out of the `available` ones.
/// This is the policy part of device initialization: it decides what gets enabled when slang-rhi creates the device,
/// and what gets used on an imported device. The result only contains feature bits of structures whose extension
/// (if any) is selected as well, so it can be passed to vkCreateDevice as is (see linkEnabledFeatures).
void selectDeviceFeatures(const VulkanFeatureSet& available, const DeviceDesc& desc, VulkanFeatureSet& outEnabled);

/// Sets up `createInfo` to enable the features in `enabled`: links all feature structures with enabled features into
/// its pNext chain and points pEnabledFeatures to the core features. `enabled` must outlive `createInfo`.
void linkEnabledFeatures(VulkanFeatureSet& enabled, VkDeviceCreateInfo& createInfo);

/// Reports the slang-rhi features and shader capabilities provided by the `enabled` extensions and features.
void reportDeviceFeatures(
    const VulkanFeatureSet& enabled,
    const VulkanDeviceProperties& properties,
    std::vector<Feature>& outFeatures,
    std::vector<Capability>& outCapabilities
);

} // namespace rhi::vk
