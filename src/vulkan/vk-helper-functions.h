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

Size calcRowSize(Format format, uint32_t width);
uint32_t calcNumRows(Format format, uint32_t height);

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
