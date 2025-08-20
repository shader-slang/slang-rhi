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

} // namespace rhi::debug
