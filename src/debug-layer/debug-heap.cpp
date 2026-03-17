#include "debug-heap.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

Result DebugHeap::allocate(const HeapAllocDesc& desc, HeapAlloc* allocation)
{
    SLANG_RHI_DEBUG_API(IHeap, allocate);

    return baseObject->allocate(desc, allocation);
}

Result DebugHeap::free(HeapAlloc allocation)
{
    SLANG_RHI_DEBUG_API(IHeap, free);

    return baseObject->free(allocation);
}

Result DebugHeap::report(HeapReport* outReport)
{
    SLANG_RHI_DEBUG_API(IHeap, report);

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
