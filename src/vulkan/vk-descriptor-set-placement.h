#pragma once

#include "vk-api.h"

#include <vector>
#include <cstdint>

namespace rhi::vk {

/// Descriptor-set placement for one occurrence of a shader object in a program.
/// Two objects with the same layout can occupy different sets. Children are indexed
/// by the reflected subobject slot, not by the order in which binding visits them.
struct DescriptorSetPlacement
{
    uint32_t firstSet = 0;
    std::vector<DescriptorSetPlacement> subObjects;
};

/// Places independently owned descriptor sets in a program's pipeline layout.
/// Parameter blocks retain their own layouts and allocate their own sets.
struct DescriptorSetPlacements
{
    DescriptorSetPlacement root;
    std::vector<DescriptorSetPlacement> entryPoints;
    // Borrowed child layouts at their absolute set numbers. Root sets and gaps
    // are filled in when the root's native pipeline layout is created.
    std::vector<VkDescriptorSetLayout> layouts;
};

} // namespace rhi::vk
