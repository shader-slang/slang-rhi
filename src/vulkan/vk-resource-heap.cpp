#include "vk-resource-heap.h"
#include "vk-device.h"
#include "vk-buffer.h"
#include "vk-utils.h"

namespace rhi::vk {

static VkMemoryPropertyFlags getMemoryProperties(MemoryType memoryType)
{
    switch (memoryType)
    {
    case MemoryType::Upload:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    case MemoryType::ReadBack:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
               VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    default:
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
}

ResourceHeapImpl::ResourceHeapImpl(Device* device, const ResourceHeapDesc& desc)
    : ResourceHeap(device, desc)
{
}

ResourceHeapImpl::~ResourceHeapImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    if (m_mapped)
        device->m_api.vkUnmapMemory(device->m_api.m_device, m_memory);
    if (m_memory)
        device->m_api.vkFreeMemory(device->m_api.m_device, m_memory, nullptr);
}

Result ResourceHeapImpl::init()
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    const VulkanApi& api = device->m_api;

    uint32_t memoryTypeBits = ~0u;
    if (m_desc.kind == ResourceHeapKind::Buffers || m_desc.kind == ResourceHeapKind::All)
    {
        VkBufferUsageFlags usage =
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (api.m_extendedFeatures.vulkan12Features.bufferDeviceAddress)
            usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        VkBuffer buffer = VK_NULL_HANDLE;
        SLANG_RETURN_ON_FAIL(createVkBuffer(api, 256, usage, 0, &buffer));
        VkMemoryRequirements reqs = {};
        api.vkGetBufferMemoryRequirements(api.m_device, buffer, &reqs);
        api.vkDestroyBuffer(api.m_device, buffer, nullptr);
        memoryTypeBits &= reqs.memoryTypeBits;
    }
    if (m_desc.kind != ResourceHeapKind::Buffers)
    {
        VkImageCreateInfo imageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent = {4, 4, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (m_desc.kind == ResourceHeapKind::RtDsTextures || m_desc.kind == ResourceHeapKind::All)
            imageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkImage image = VK_NULL_HANDLE;
        SLANG_VK_RETURN_ON_FAIL_REPORT(api.vkCreateImage(api.m_device, &imageInfo, nullptr, &image), device);
        VkMemoryRequirements reqs = {};
        api.vkGetImageMemoryRequirements(api.m_device, image, &reqs);
        api.vkDestroyImage(api.m_device, image, nullptr);
        memoryTypeBits &= reqs.memoryTypeBits;
    }
    if (m_desc.kind == ResourceHeapKind::RtDsTextures || m_desc.kind == ResourceHeapKind::All)
    {
        VkImageCreateInfo imageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_D32_SFLOAT;
        imageInfo.extent = {4, 4, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkImage image = VK_NULL_HANDLE;
        SLANG_VK_RETURN_ON_FAIL_REPORT(api.vkCreateImage(api.m_device, &imageInfo, nullptr, &image), device);
        VkMemoryRequirements reqs = {};
        api.vkGetImageMemoryRequirements(api.m_device, image, &reqs);
        api.vkDestroyImage(api.m_device, image, nullptr);
        memoryTypeBits &= reqs.memoryTypeBits;
    }

    const VkMemoryPropertyFlags properties = getMemoryProperties(m_desc.memoryType);
    int memoryTypeIndex = api.findMemoryTypeIndex(memoryTypeBits, properties);
    if (memoryTypeIndex < 0)
        return SLANG_E_NOT_AVAILABLE;
    m_memoryTypeIndex = (uint32_t)memoryTypeIndex;

    VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = m_desc.size;
    allocInfo.memoryTypeIndex = m_memoryTypeIndex;

    VkMemoryAllocateFlagsInfo flagsInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    if (api.m_extendedFeatures.vulkan12Features.bufferDeviceAddress)
    {
        flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        allocInfo.pNext = &flagsInfo;
    }

    SLANG_VK_RETURN_ON_FAIL_REPORT(api.vkAllocateMemory(api.m_device, &allocInfo, nullptr, &m_memory), device);

    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        SLANG_VK_RETURN_ON_FAIL_REPORT(api.vkMapMemory(api.m_device, m_memory, 0, m_desc.size, 0, &m_mapped), device);
    }

    if (m_desc.label)
        device->_labelObject((uint64_t)m_memory, VK_OBJECT_TYPE_DEVICE_MEMORY, m_desc.label);

    return SLANG_OK;
}

Result ResourceHeapImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkDeviceMemory;
    outHandle->value = (uint64_t)m_memory;
    return SLANG_OK;
}

} // namespace rhi::vk
