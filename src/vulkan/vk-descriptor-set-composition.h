#pragma once

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

/// Maps a program's object tree to a single, composed descriptor-set layout.
/// The root allocates all sets, including gaps and sets shared by added bindings.
/// Runtime binding only consumes placements; it does not reconstruct allocation order.
struct DescriptorSetComposition
{
    DescriptorSetPlacement root;
    std::vector<DescriptorSetPlacement> entryPoints;
};

} // namespace rhi::vk
