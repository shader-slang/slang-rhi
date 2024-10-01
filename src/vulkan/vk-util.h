#pragma once

#include <slang-rhi.h>

#include "vk-api.h"

#include "core/common.h"

#include <vector>

// Macros to make testing vulkan return codes simpler

/// SLANG_VK_RETURN_ON_FAIL can be used in a similar way to SLANG_RETURN_ON_FAIL macro, except it will turn a vulkan
/// failure into Result in the process Calls handleFail which on debug builds asserts
#define SLANG_VK_RETURN_ON_FAIL(x)                                                                                     \
    {                                                                                                                  \
        VkResult _res = x;                                                                                             \
        if (_res != VK_SUCCESS)                                                                                        \
        {                                                                                                              \
            return ::rhi::vk::VulkanUtil::handleFail(_res);                                                            \
        }                                                                                                              \
    }

#define SLANG_VK_RETURN_NULL_ON_FAIL(x)                                                                                \
    {                                                                                                                  \
        VkResult _res = x;                                                                                             \
        if (_res != VK_SUCCESS)                                                                                        \
        {                                                                                                              \
            ::rhi::vk::VulkanUtil::handleFail(_res);                                                                   \
            return nullptr;                                                                                            \
        }                                                                                                              \
    }

/// Is similar to SLANG_VK_RETURN_ON_FAIL, but does not return. Will call checkFail on failure - which asserts on debug
/// builds.
#define SLANG_VK_CHECK(x)                                                                                              \
    {                                                                                                                  \
        VkResult _res = x;                                                                                             \
        if (_res != VK_SUCCESS)                                                                                        \
        {                                                                                                              \
            ::rhi::vk::VulkanUtil::checkFail(_res);                                                                    \
        }                                                                                                              \
    }

namespace rhi::vk {

// Utility functions for Vulkan
struct VulkanUtil
{
    /// Get the equivalent VkFormat from the format
    /// Returns VK_FORMAT_UNDEFINED if a match is not found
    static VkFormat getVkFormat(Format format);

    static VkImageAspectFlags getAspectMask(TextureAspect aspect, VkFormat format);

    /// Called by SLANG_VK_RETURN_FAIL if a res is a failure.
    /// On debug builds this will cause an assertion on failure.
    static Result handleFail(VkResult res);
    /// Called when a failure has occurred with SLANG_VK_CHECK - will typically assert.
    static void checkFail(VkResult res);

    /// Get the VkPrimitiveTopology for the given topology.
    /// Returns VK_PRIMITIVE_TOPOLOGY_MAX_ENUM on failure
    static VkPrimitiveTopology getVkPrimitiveTopology(PrimitiveTopology topology);

    static VkImageLayout mapResourceStateToLayout(ResourceState state);

    /// Returns Result equivalent of a VkResult
    static Result toResult(VkResult res);

    static VkShaderStageFlags getShaderStage(SlangStage stage);

    static VkImageLayout getImageLayoutFromState(ResourceState state);

    /// Calculate size taking into account alignment. Alignment must be a power of 2
    static UInt calcAligned(UInt size, UInt alignment) { return (size + alignment - 1) & ~(alignment - 1); }

    static inline bool isDepthFormat(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        }
        return false;
    }

    static inline bool isStencilFormat(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        }
        return false;
    }

    static VkSampleCountFlagBits translateSampleCount(uint32_t sampleCount);

    static VkCullModeFlags translateCullMode(CullMode cullMode);

    static VkFrontFace translateFrontFaceMode(FrontFaceMode frontFaceMode);

    static VkPolygonMode translateFillMode(FillMode fillMode);

    static VkBlendFactor translateBlendFactor(BlendFactor blendFactor);

    static VkBlendOp translateBlendOp(BlendOp op);

    static VkPrimitiveTopology translatePrimitiveListTopology(PrimitiveTopology topology);

    static VkStencilOp translateStencilOp(StencilOp op);

    static VkFilter translateFilterMode(TextureFilteringMode mode);

    static VkSamplerMipmapMode translateMipFilterMode(TextureFilteringMode mode);

    static VkSamplerAddressMode translateAddressingMode(TextureAddressingMode mode);

    static VkCompareOp translateComparisonFunc(ComparisonFunc func);

    static VkStencilOpState translateStencilState(DepthStencilOpDesc desc);

    static VkSamplerReductionMode translateReductionOp(TextureReductionOp op);
};

struct AccelerationStructureBuildGeometryInfoBuilder
{
public:
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR
    };
    std::vector<uint32_t> primitiveCounts;

    Result build(const AccelerationStructureBuildDesc& buildDesc, IDebugCallback* debugCallback);

private:
    std::vector<VkAccelerationStructureGeometryKHR> geometries;

    VkBuildAccelerationStructureFlagsKHR translateBuildFlags(AccelerationStructureBuildFlags flags)
    {
        VkBuildAccelerationStructureFlagsKHR result = VkBuildAccelerationStructureFlagsKHR(0);
        if (is_set(flags, AccelerationStructureBuildFlags::AllowCompaction))
        {
            result |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
        }
        if (is_set(flags, AccelerationStructureBuildFlags::AllowUpdate))
        {
            result |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        }
        if (is_set(flags, AccelerationStructureBuildFlags::MinimizeMemory))
        {
            result |= VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR;
        }
        if (is_set(flags, AccelerationStructureBuildFlags::PreferFastBuild))
        {
            result |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
        }
        if (is_set(flags, AccelerationStructureBuildFlags::PreferFastTrace))
        {
            result |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        }
        return result;
    }
    VkGeometryFlagsKHR translateGeometryFlags(AccelerationStructureGeometryFlags flags)
    {
        VkGeometryFlagsKHR result = VkGeometryFlagsKHR(0);
        if (is_set(flags, AccelerationStructureGeometryFlags::Opaque))
            result |= VK_GEOMETRY_OPAQUE_BIT_KHR;
        if (is_set(flags, AccelerationStructureGeometryFlags::NoDuplicateAnyHitInvocation))
            result |= VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
        return result;
    }
};

} // namespace rhi::vk
