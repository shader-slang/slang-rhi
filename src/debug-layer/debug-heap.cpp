#include "debug-heap.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

Result DebugHeap::allocate(const HeapAllocDesc& desc, HeapAlloc* outAllocation)
{
    SLANG_RHI_DEBUG_API(IHeap, allocate);

    if (!outAllocation)
    {
        RHI_VALIDATION_ERROR("'outAllocation' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.size == 0)
    {
        RHI_VALIDATION_ERROR("Heap allocation size must be greater than 0.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.alignment != 0 && (desc.alignment & (desc.alignment - 1)) != 0)
    {
        RHI_VALIDATION_ERROR("Heap allocation alignment must be 0 or a power of 2.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->allocate(desc, outAllocation);
}

Result DebugHeap::free(HeapAlloc allocation)
{
    SLANG_RHI_DEBUG_API(IHeap, free);

    if (!allocation.isValid())
    {
        RHI_VALIDATION_WARNING("Allocation is not valid.");
        return SLANG_OK;
    }

    return baseObject->free(allocation);
}

Result DebugHeap::report(HeapReport* outReport)
{
    SLANG_RHI_DEBUG_API(IHeap, report);

    if (!outReport)
    {
        RHI_VALIDATION_ERROR("'outReport' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->report(outReport);
}

Result DebugHeap::flush()
{
    SLANG_RHI_DEBUG_API(IHeap, flush);

    return baseObject->flush();
}

Result DebugHeap::removeEmptyPages()
{
    SLANG_RHI_DEBUG_API(IHeap, removeEmptyPages);

    return baseObject->removeEmptyPages();
}

} // namespace rhi::debug
