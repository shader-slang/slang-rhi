#include "resource-heap.h"

#include "rhi-shared.h"

namespace rhi {

ResourceHeap::ResourceHeap(Device* device, const ResourceHeapDesc& desc)
    : DeviceChild(device)
{
    m_desc = desc;
    m_desc.next = nullptr;
    m_descHolder.holdString(m_desc.label);

    if (desc.requirements && desc.requirementCount > 0)
    {
        m_requirements.assign(desc.requirements, desc.requirements + desc.requirementCount);
        for (ResourceMemoryRequirements& requirements : m_requirements)
        {
            // Output extension chains are owned by the caller and are creation-only here.
            requirements.next = nullptr;
            if (desc.usage == ResourceHeapUsage::None)
                m_desc.usage |= requirements.usage;
            m_desc.alignment = max(m_desc.alignment, requirements.heapAlignment);
        }
        m_desc.requirements = m_requirements.data();
    }
    else
    {
        m_desc.requirements = nullptr;
        m_desc.requirementCount = 0;
    }
}

Result ResourceHeap::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

bool ResourceHeap::isCompatible(const ResourceMemoryRequirements& requirements) const
{
    if (requirements.structType != ResourceMemoryRequirements::kStructType)
        return false;
    if (!isResourceHeapCompatibilityFromDevice(requirements.compatibility, getDevice()))
        return false;
    if (is_set(requirements.flags, ResourceMemoryRequirementFlags::RequiresDedicatedAllocation))
        return false;
    if (requirements.size == 0 || requirements.alignment == 0 || requirements.heapAlignment == 0)
        return false;
    if (requirements.size > m_desc.size)
        return false;
    if (m_desc.memoryType != requirements.memoryType)
        return false;
    if (!isResourceHeapUsageCompatible(m_desc.usage, requirements.usage))
        return false;
    if (m_desc.alignment < requirements.heapAlignment)
        return false;
    return true;
}

ResourceHeapCompatibility makeResourceHeapCompatibility(Device* device, uint64_t backendMask)
{
    ResourceHeapCompatibility result = {};
    result.data[0] = backendMask;
    result.data[1] = uint64_t(reinterpret_cast<uintptr_t>(device));
    return result;
}

bool isResourceHeapCompatibilityFromDevice(const ResourceHeapCompatibility& compatibility, const Device* device)
{
    return compatibility.data[1] == uint64_t(reinterpret_cast<uintptr_t>(device));
}

uint64_t getResourceHeapCompatibilityMask(const ResourceHeapCompatibility& compatibility)
{
    return compatibility.data[0];
}

void resetResourceMemoryRequirements(ResourceMemoryRequirements* outRequirements)
{
    void* next = outRequirements->next;
    *outRequirements = {};
    outRequirements->next = next;
}

Result validateResourceHeapDesc(Device* device, const ResourceHeapDesc& desc)
{
    if (desc.structType != ResourceHeapDesc::kStructType)
        return SLANG_E_INVALID_ARG;
    if (desc.size == 0)
        return SLANG_E_INVALID_ARG;
    if (desc.requirementCount == 0 || !desc.requirements)
        return SLANG_E_INVALID_ARG;

    const ResourceHeapUsage validUsage = ResourceHeapUsage::All;
    if ((desc.usage & ~validUsage) != ResourceHeapUsage::None)
        return SLANG_E_INVALID_ARG;
    if (desc.alignment != 0 && (desc.alignment & (desc.alignment - 1)) != 0)
        return SLANG_E_INVALID_ARG;

    ResourceHeapUsage requiredUsage = ResourceHeapUsage::None;
    Size requiredHeapAlignment = 1;
    for (uint32_t i = 0; i < desc.requirementCount; ++i)
    {
        const ResourceMemoryRequirements& requirements = desc.requirements[i];
        if (requirements.structType != ResourceMemoryRequirements::kStructType)
            return SLANG_E_INVALID_ARG;
        if (requirements.usage == ResourceHeapUsage::None ||
            (requirements.usage & ~validUsage) != ResourceHeapUsage::None)
            return SLANG_E_INVALID_ARG;
        const ResourceMemoryRequirementFlags validFlags = ResourceMemoryRequirementFlags::RequiresDedicatedAllocation |
                                                          ResourceMemoryRequirementFlags::PrefersDedicatedAllocation;
        if ((requirements.flags & ~validFlags) != ResourceMemoryRequirementFlags::None)
            return SLANG_E_INVALID_ARG;
        if (!isResourceHeapCompatibilityFromDevice(requirements.compatibility, device))
            return SLANG_E_INVALID_ARG;
        if (is_set(requirements.flags, ResourceMemoryRequirementFlags::RequiresDedicatedAllocation))
            return SLANG_E_NOT_AVAILABLE;
        if (requirements.memoryType != desc.memoryType || requirements.size == 0 || requirements.alignment == 0 ||
            requirements.heapAlignment == 0)
            return SLANG_E_INVALID_ARG;
        if ((requirements.alignment & (requirements.alignment - 1)) != 0 ||
            (requirements.heapAlignment & (requirements.heapAlignment - 1)) != 0)
            return SLANG_E_INVALID_ARG;
        if (requirements.size > desc.size)
            return SLANG_E_INVALID_ARG;
        requiredUsage |= requirements.usage;
        requiredHeapAlignment = max(requiredHeapAlignment, requirements.heapAlignment);
    }

    if (desc.usage != ResourceHeapUsage::None && !isResourceHeapUsageCompatible(desc.usage, requiredUsage))
        return SLANG_E_INVALID_ARG;
    if (desc.alignment != 0 && desc.alignment < requiredHeapAlignment)
        return SLANG_E_INVALID_ARG;
    return SLANG_OK;
}

const ResourcePlacementDesc* findResourcePlacementDesc(const void* next)
{
    for (const ChainedStructHeader* header = static_cast<const ChainedStructHeader*>(next); header;
         header = static_cast<const ChainedStructHeader*>(header->next))
    {
        if (header->type == StructType::ResourcePlacementDesc)
            return reinterpret_cast<const ResourcePlacementDesc*>(header);
    }
    return nullptr;
}

Result validateResourcePlacement(
    Device* device,
    const ResourcePlacementDesc& placement,
    const ResourceMemoryRequirements& requirements
)
{
    if (!placement.heap)
        return SLANG_E_INVALID_ARG;
    ResourceHeap* heap = checked_cast<ResourceHeap*>(placement.heap);
    if (heap->getDevice() != device)
        return SLANG_E_INVALID_ARG;
    if (requirements.size == 0 || requirements.alignment == 0)
        return SLANG_E_INVALID_ARG;
    if ((placement.offset % requirements.alignment) != 0)
        return SLANG_E_INVALID_ARG;

    const ResourceHeapDesc& heapDesc = heap->getDesc();
    if (!heap->isCompatible(requirements))
        return SLANG_E_INVALID_ARG;
    if (placement.offset > heapDesc.size || requirements.size > heapDesc.size - placement.offset)
        return SLANG_E_INVALID_ARG;

    return SLANG_OK;
}

} // namespace rhi
