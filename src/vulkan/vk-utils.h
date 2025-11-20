#pragma once

#include <slang-rhi.h>

#include "vk-api.h"

#include "core/common.h"

namespace rhi::vk {

// Macros to make testing vulkan return codes simpler

void reportVulkanError(VkResult res);

/// SLANG_VK_RETURN_ON_FAIL can be used in a similar way to SLANG_RETURN_ON_FAIL macro.
/// Asserts on debug builds.
#define SLANG_VK_RETURN_ON_FAIL(x)                                                                                     \
    {                                                                                                                  \
        VkResult _res = x;                                                                                             \
        if (_res != VK_SUCCESS)                                                                                        \
        {                                                                                                              \
            ::rhi::vk::reportVulkanError(_res);                                                                        \
            return SLANG_FAIL;                                                                                         \
        }                                                                                                              \
    }

/// Is similar to SLANG_VK_RETURN_ON_FAIL, but does not return.
#define SLANG_VK_CHECK(x)                                                                                              \
    {                                                                                                                  \
        VkResult _res = x;                                                                                             \
        if (_res != VK_SUCCESS)                                                                                        \
        {                                                                                                              \
            ::rhi::vk::reportVulkanError(_res);                                                                        \
        }                                                                                                              \
    }

// Utility functions for Vulkan

/// Get the equivalent VkFormat from the format
/// Returns VK_FORMAT_UNDEFINED if a match is not found
VkFormat getVkFormat(Format format);

VkAttachmentLoadOp translateLoadOp(LoadOp loadOp);
VkAttachmentStoreOp translateStoreOp(StoreOp storeOp);
VkPipelineCreateFlags translateRayTracingPipelineFlags(RayTracingPipelineFlags flags);
VkPipelineCreateFlags2 translateRayTracingPipelineFlags2(RayTracingPipelineFlags flags);

VkImageLayout translateImageLayout(ResourceState state);

VkAccessFlagBits calcAccessFlags(ResourceState state);
VkPipelineStageFlagBits calcPipelineStageFlags(ResourceState state, bool src);
VkAccessFlags translateAccelerationStructureAccessFlag(AccessFlag access);

VkBufferUsageFlagBits _calcBufferUsageFlags(BufferUsage usage);
VkImageUsageFlagBits _calcImageUsageFlags(ResourceState state);
VkImageUsageFlagBits _calcImageUsageFlags(TextureUsage usage);
VkImageUsageFlags _calcImageUsageFlags(TextureUsage usage, MemoryType memoryType, const void* initData);

VkAccessFlags calcAccessFlagsFromImageLayout(VkImageLayout layout);
VkPipelineStageFlags calcPipelineStageFlagsFromImageLayout(VkImageLayout layout);

VkImageAspectFlags getAspectMaskFromFormat(VkFormat format, TextureAspect aspect = TextureAspect::All);

AdapterLUID getAdapterLUID(const VkPhysicalDeviceIDProperties& props);

VkShaderStageFlags translateShaderStage(SlangStage stage);

VkImageLayout getImageLayoutFromState(ResourceState state);

bool isDepthFormat(VkFormat format);
bool isStencilFormat(VkFormat format);

VkSampleCountFlagBits translateSampleCount(uint32_t sampleCount);

VkCullModeFlags translateCullMode(CullMode cullMode);

VkFrontFace translateFrontFaceMode(FrontFaceMode frontFaceMode);

VkPolygonMode translateFillMode(FillMode fillMode);

VkBlendFactor translateBlendFactor(BlendFactor blendFactor);

VkBlendOp translateBlendOp(BlendOp op);

VkPrimitiveTopology translatePrimitiveListTopology(PrimitiveTopology topology);

VkStencilOp translateStencilOp(StencilOp op);

VkFilter translateFilterMode(TextureFilteringMode mode);

VkSamplerMipmapMode translateMipFilterMode(TextureFilteringMode mode);

VkSamplerAddressMode translateAddressingMode(TextureAddressingMode mode);

VkCompareOp translateComparisonFunc(ComparisonFunc func);

VkStencilOpState translateStencilState(DepthStencilOpDesc desc);

VkSamplerReductionMode translateReductionOp(TextureReductionOp op);

VkComponentTypeKHR translateCooperativeVectorComponentType(CooperativeVectorComponentType type);
CooperativeVectorComponentType translateCooperativeVectorComponentType(VkComponentTypeKHR type);
VkCooperativeVectorMatrixLayoutNV translateCooperativeVectorMatrixLayout(CooperativeVectorMatrixLayout layout);
CooperativeVectorMatrixLayout translateCooperativeVectorMatrixLayout(VkCooperativeVectorMatrixLayoutNV layout);

} // namespace rhi::vk
