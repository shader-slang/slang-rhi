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
        memoryTypeBits &= uint32_t(getResourceHeapCompatibilityMask(m_desc.requirements[i].compatibility));

    const VkMemoryPropertyFlags properties = getMemoryProperties(m_desc.memoryType);
    int memoryTypeIndex = api.findMemoryTypeIndex(memoryTypeBits, properties);
    if (memoryTypeIndex < 0)
        return SLANG_E_NOT_AVAILABLE;
    m_memoryTypeIndex = (uint32_t)memoryTypeIndex;
    m_desc.alignment = max<Size>(m_desc.alignment, 1);

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

bool ResourceHeapImpl::isCompatible(const ResourceMemoryRequirements& requirements) const
{
    if (!ResourceHeap::isCompatible(requirements))
        return false;
    const uint64_t memoryTypeBits = getResourceHeapCompatibilityMask(requirements.compatibility);
    return (memoryTypeBits & (uint64_t(1) << m_memoryTypeIndex)) != 0;
}

} // namespace rhi::vk
