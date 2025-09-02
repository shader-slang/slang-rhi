#include "debug-heap.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

Result DebugHeap::allocate(const HeapAllocDesc& desc, HeapAlloc* allocation)
{
    SLANG_RHI_API_FUNC;
    return baseObject->allocate(desc, allocation);
}

Result DebugHeap::free(HeapAlloc allocation)
{
    SLANG_RHI_API_FUNC;
    return baseObject->free(allocation);
}

Result DebugHeap::report(HeapReport* outReport)
{
    SLANG_RHI_API_FUNC;
    return baseObject->report(outReport);
}

Result DebugHeap::flush()
{
    SLANG_RHI_API_FUNC;
    return baseObject->flush();
}

Result DebugHeap::removeEmptyPages()
{
    SLANG_RHI_API_FUNC;
    return baseObject->removeEmptyPages();
}

} // namespace rhi::debug
