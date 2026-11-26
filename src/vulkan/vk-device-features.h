#pragma once

#include "vk-api.h"

#include <set>
#include <string>

namespace rhi::vk {

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

} // namespace rhi::vk
