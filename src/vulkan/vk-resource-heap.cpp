#include "vk-resource-heap.h"
#include "vk-device.h"
#include "vk-buffer.h"
#include "vk-utils.h"

namespace rhi::vk {

ResourceHeapImpl::ResourceHeapImpl(Device* device, const ResourceHeapDesc& desc)
    : ResourceHeap(device, desc)
{
}

ResourceHeapImpl::~ResourceHeapImpl() {}

Result ResourceHeapImpl::init()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    uint32_t memoryTypeBits = ~0u;
    for (uint32_t i = 0; i < m_desc.requirementCount; ++i)
        memoryTypeBits &= uint32_t(getResourceHeapCompatibilityMask(m_desc.requirements[i]));

    if (memoryTypeBits == 0)
        return SLANG_E_NOT_AVAILABLE;

    if (m_desc.alignment > 1)
        return SLANG_E_INVALID_ARG;
    m_desc.alignment = max<Size>(m_desc.alignment, 1);

    // Resources are bound to the heap's memory at explicit offsets, so the heap needs its own
    // VkDeviceMemory that is not shared with other allocations.
    VkMemoryRequirements memoryRequirements = {};
    memoryRequirements.size = m_desc.size;
    memoryRequirements.alignment = 1;
    memoryRequirements.memoryTypeBits = memoryTypeBits;

    AllocationDesc allocationDesc;
    allocationDesc.memoryType = m_desc.memoryType;
    allocationDesc.dedicated = true;
    SLANG_RETURN_ON_FAIL(device->m_memoryAllocator.allocateMemory(memoryRequirements, allocationDesc, m_allocation));
    SLANG_RHI_ASSERT(m_allocation.getOffset() == 0);

    if (m_desc.label)
        device->_labelObject((uint64_t)m_allocation.getDeviceMemory(), VK_OBJECT_TYPE_DEVICE_MEMORY, m_desc.label);

    return SLANG_OK;
}

Result ResourceHeapImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkDeviceMemory;
    outHandle->value = (uint64_t)m_allocation.getDeviceMemory();
    return SLANG_OK;
}

bool ResourceHeapImpl::isCompatible(const ResourceMemoryRequirements& requirements) const
{
    if (!ResourceHeap::isCompatible(requirements))
        return false;
    const uint64_t memoryTypeBits = getResourceHeapCompatibilityMask(requirements);
    return (memoryTypeBits & (uint64_t(1) << m_allocation.getMemoryTypeIndex())) != 0;
}

} // namespace rhi::vk
