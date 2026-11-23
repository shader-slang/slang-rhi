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
    if (m_vmaAllocation)
    {
        if (m_mapped)
            vmaUnmapMemory(device->m_vmaAllocator, m_vmaAllocation);
        vmaFreeMemory(device->m_vmaAllocator, m_vmaAllocation);
    }
}

Result ResourceHeapImpl::init()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    uint32_t memoryTypeBits = ~0u;
    for (uint32_t i = 0; i < m_desc.requirementCount; ++i)
        memoryTypeBits &= uint32_t(getResourceHeapCompatibilityMask(m_desc.requirements[i]));

    if (memoryTypeBits == 0)
        return SLANG_E_NOT_AVAILABLE;

    const VkMemoryPropertyFlags properties = getMemoryProperties(m_desc.memoryType);
    if (m_desc.alignment > 1)
        return SLANG_E_INVALID_ARG;
    m_desc.alignment = max<Size>(m_desc.alignment, 1);

    // Allocate a dedicated block of device memory. Resources are bound to it at
    // explicit offsets, so the block must not be shared with other VMA allocations.
    VkMemoryRequirements memoryRequirements = {};
    memoryRequirements.size = m_desc.size;
    memoryRequirements.alignment = 1;
    memoryRequirements.memoryTypeBits = memoryTypeBits;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    allocCreateInfo.requiredFlags = properties;
    // Readback memory is read by the CPU, so prefer a cached memory type.
    // VMA falls back to a required-only memory type if the preferred one is exhausted.
    if (m_desc.memoryType == MemoryType::ReadBack)
        allocCreateInfo.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

    VmaAllocationInfo allocInfo = {};
    VkResult result = vmaAllocateMemory(
        device->m_vmaAllocator,
        &memoryRequirements,
        &allocCreateInfo,
        &m_vmaAllocation,
        &allocInfo
    );
    // No memory type is compatible with all requirements and the requested memory type.
    if (result == VK_ERROR_FEATURE_NOT_PRESENT)
        return SLANG_E_NOT_AVAILABLE;
    SLANG_VK_RETURN_ON_FAIL_REPORT(result, device);
    m_memory = allocInfo.deviceMemory;
    m_memoryTypeIndex = allocInfo.memoryType;

    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        SLANG_VK_RETURN_ON_FAIL_REPORT(vmaMapMemory(device->m_vmaAllocator, m_vmaAllocation, &m_mapped), device);
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
