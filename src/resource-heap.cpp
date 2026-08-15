#include "resource-heap.h"

#include "rhi-shared.h"

namespace rhi {

ResourceHeap::ResourceHeap(Device* device, const ResourceHeapDesc& desc)
    : DeviceChild(device)
{
    m_desc = desc;
    m_descHolder.holdString(m_desc.label);
}

Result ResourceHeap::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

const ResourcePlacementDesc* findResourcePlacementDesc(const void* next)
{
    for (const DescStructHeader* header = static_cast<const DescStructHeader*>(next); header; header = header->next)
    {
        if (header->type == StructType::ResourcePlacementDesc)
            return reinterpret_cast<const ResourcePlacementDesc*>(header);
    }
    return nullptr;
}

Result validateResourcePlacement(const ResourcePlacementDesc& placement, const ResourceMemoryRequirements& requirements)
{
    if (!placement.heap)
        return SLANG_E_INVALID_ARG;
    if (requirements.requiresDedicatedAllocation)
        return SLANG_E_INVALID_ARG;
    if (requirements.size == 0 || requirements.alignment == 0)
        return SLANG_E_INVALID_ARG;
    if ((placement.offset % requirements.alignment) != 0)
        return SLANG_E_INVALID_ARG;

    const ResourceHeapDesc& heapDesc = placement.heap->getDesc();
    if (heapDesc.memoryType != requirements.memoryType)
        return SLANG_E_INVALID_ARG;
    if (!isResourceHeapKindCompatible(heapDesc.kind, requirements.heapKind))
        return SLANG_E_INVALID_ARG;
    if (placement.offset > heapDesc.size || requirements.size > heapDesc.size - placement.offset)
        return SLANG_E_INVALID_ARG;

    return SLANG_OK;
}

} // namespace rhi
