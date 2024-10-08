#pragma once

#include "vk-base.h"
#include "vk-util.h"

#include "core/common.h"

#include <vector>

#ifdef _MSC_VER
#include <stddef.h>
#pragma warning(disable : 4996)
#if (_MSC_VER < 1900)
#define snprintf sprintf_s
#endif
#endif

#if SLANG_WINDOWS_FAMILY
#include <dxgi1_2.h>
#endif

namespace rhi::vk {

// In order to bind shader parameters to the correct locations, we need to
// be able to describe those locations. Most shader parameters in Vulkan
// simply consume a single `binding`, but we also need to deal with
// parameters that represent push-constant ranges.
//
// In more complex cases we might be binding an entire "sub-object" like
// a parameter block, an entry point, etc. For the general case, we need
// to be able to represent a composite offset that includes offsets for
// each of the cases that Vulkan supports.

/// A "simple" binding offset that records `binding`, `set`, etc. offsets
struct SimpleBindingOffset
{
    /// An offset in GLSL/SPIR-V `binding`s
    uint32_t binding = 0;

    /// The descriptor `set` that the `binding` field should be understood as an index into
    uint32_t bindingSet = 0;

    /// The offset in push-constant ranges (not bytes)
    uint32_t pushConstantRange = 0;

    /// Create a default (zero) offset
    SimpleBindingOffset() {}

    /// Create an offset based on offset information in the given Slang `varLayout`
    SimpleBindingOffset(slang::VariableLayoutReflection* varLayout)
    {
        if (varLayout)
        {
            bindingSet = (uint32_t)varLayout->getBindingSpace(SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT);
            binding = (uint32_t)varLayout->getOffset(SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT);
            pushConstantRange = (uint32_t)varLayout->getOffset(SLANG_PARAMETER_CATEGORY_PUSH_CONSTANT_BUFFER);
        }
    }

    /// Add any values in the given `offset`
    void operator+=(SimpleBindingOffset const& offset)
    {
        binding += offset.binding;
        bindingSet += offset.bindingSet;
        pushConstantRange += offset.pushConstantRange;
    }
};

// While a "simple" binding offset representation will work in many cases,
// once we need to deal with layout for programs with interface-type parameters
// that have been statically specialized, we also need to track the offset
// for where to bind any "pending" data that arises from the process of static
// specialization.
//
// In order to conveniently track both the "primary" and "pending" offset information,
// we will define a more complete `BindingOffset` type that combines simple
// binding offsets for the primary and pending parts.

/// A representation of the offset at which to bind a shader parameter or sub-object
struct BindingOffset : SimpleBindingOffset
{
    // Offsets for "primary" data are stored directly in the `BindingOffset`
    // via the inheritance from `SimpleBindingOffset`.

    /// Offset for any "pending" data
    SimpleBindingOffset pending;

    /// Create a default (zero) offset
    BindingOffset() {}

    /// Create an offset from a simple offset
    explicit BindingOffset(SimpleBindingOffset const& offset)
        : SimpleBindingOffset(offset)
    {
    }

    /// Create an offset based on offset information in the given Slang `varLayout`
    BindingOffset(slang::VariableLayoutReflection* varLayout)
        : SimpleBindingOffset(varLayout)
        , pending(varLayout->getPendingDataLayout())
    {
    }

    /// Add any values in the given `offset`
    void operator+=(SimpleBindingOffset const& offset) { SimpleBindingOffset::operator+=(offset); }

    /// Add any values in the given `offset`
    void operator+=(BindingOffset const& offset)
    {
        SimpleBindingOffset::operator+=(offset);
        pending += offset.pending;
    }
};

/// Context information required when binding shader objects to the pipeline
struct RootBindingContext
{
    /// The pipeline layout being used for binding
    VkPipelineLayout pipelineLayout;

    /// An allocator to use for descriptor sets during binding
    DescriptorSetAllocator* descriptorSetAllocator;

    /// The device being used
    DeviceImpl* device;

    /// The descriptor sets that are being allocated and bound
    std::vector<VkDescriptorSet>* descriptorSets;

    /// Information about all the push-constant ranges that should be bound
    span<const VkPushConstantRange> pushConstantRanges;
};

Size calcRowSize(Format format, int width);
GfxCount calcNumRows(Format format, int height);

VkAttachmentLoadOp translateLoadOp(LoadOp loadOp);
VkAttachmentStoreOp translateStoreOp(StoreOp storeOp);
VkPipelineCreateFlags translateRayTracingPipelineFlags(RayTracingPipelineFlags flags);

uint32_t getMipLevelSize(uint32_t mipLevel, uint32_t size);
VkImageLayout translateImageLayout(ResourceState state);

VkAccessFlagBits calcAccessFlags(ResourceState state);
VkPipelineStageFlagBits calcPipelineStageFlags(ResourceState state, bool src);
VkAccessFlags translateAccelerationStructureAccessFlag(AccessFlag access);

VkBufferUsageFlagBits _calcBufferUsageFlags(BufferUsage usage);
VkImageUsageFlagBits _calcImageUsageFlags(ResourceState state);
VkImageViewType _calcImageViewType(TextureType type, const TextureDesc& desc);
VkImageUsageFlagBits _calcImageUsageFlags(TextureUsage usage);
VkImageUsageFlags _calcImageUsageFlags(TextureUsage usage, MemoryType memoryType, const void* initData);

VkAccessFlags calcAccessFlagsFromImageLayout(VkImageLayout layout);
VkPipelineStageFlags calcPipelineStageFlagsFromImageLayout(VkImageLayout layout);

VkImageAspectFlags getAspectMaskFromFormat(VkFormat format, TextureAspect aspect = TextureAspect::All);

AdapterLUID getAdapterLUID(VulkanApi api, VkPhysicalDevice physicaDevice);

} // namespace rhi::vk

namespace rhi {

Result SLANG_MCALL getVKAdapters(std::vector<AdapterInfo>& outAdapters);

Result SLANG_MCALL createVKDevice(const DeviceDesc* desc, IDevice** outRenderer);

} // namespace rhi
