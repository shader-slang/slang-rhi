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
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
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
    for (uint32_t i = 0; i < m_desc.requirementCount; ++i)
        memoryTypeBits &= uint32_t(getResourceHeapCompatibilityMask(m_desc.requirements[i]));

    if (memoryTypeBits == 0)
        return SLANG_E_NOT_AVAILABLE;

    const VkMemoryPropertyFlags properties = getMemoryProperties(m_desc.memoryType);
    if (m_desc.alignment > 1)
        return SLANG_E_INVALID_ARG;
    m_desc.alignment = max<Size>(m_desc.alignment, 1);

    VkMemoryRequirements memoryRequirements = {};
    memoryRequirements.size = m_desc.size;
    memoryRequirements.memoryTypeBits = memoryTypeBits;
    const VkMemoryPropertyFlags preferredProperties =
        m_desc.memoryType == MemoryType::ReadBack ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT : 0;
    SLANG_RETURN_ON_FAIL(allocateVkMemory(
        api,
        memoryRequirements,
        properties,
        preferredProperties,
        api.m_extendedFeatures.vulkan12Features.bufferDeviceAddress,
        0,
        &m_memory,
        &m_memoryTypeIndex
    ));

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

bool ResourceHeapImpl::isCompatible(const ResourceMemoryRequirements& requirements) const
{
    if (!ResourceHeap::isCompatible(requirements))
        return false;
    const uint64_t memoryTypeBits = getResourceHeapCompatibilityMask(requirements);
    return (memoryTypeBits & (uint64_t(1) << m_memoryTypeIndex)) != 0;
}

} // namespace rhi::vk
