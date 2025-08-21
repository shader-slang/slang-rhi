#include "debug-graphics-heap.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

Result DebugGraphicsHeap::allocate(const GraphicsAllocDesc& desc, GraphicsAllocation* allocation)
{
    SLANG_RHI_API_FUNC;
    return baseObject->allocate(desc, allocation);
}

Result DebugGraphicsHeap::free(GraphicsAllocation allocation)
{
    SLANG_RHI_API_FUNC;
    return baseObject->free(allocation);
}

IGraphicsHeap::Report DebugGraphicsHeap::report()
{
    SLANG_RHI_API_FUNC;
    return baseObject->report();
}

Result DebugGraphicsHeap::flush()
{
    SLANG_RHI_API_FUNC;
    return baseObject->flush();
}

Result DebugGraphicsHeap::cleanUp()
{
    SLANG_RHI_API_FUNC;
    return baseObject->cleanUp();
}

} // namespace rhi::debug
