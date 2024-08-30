#include "debug-query.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

Result DebugQueryPool::getResult(GfxIndex index, GfxCount count, uint64_t* data)
{
    SLANG_RHI_API_FUNC;

    if (index < 0 || index + count > desc.count)
        RHI_VALIDATION_ERROR("index is out of bounds.");
    return baseObject->getResult(index, count, data);
}

Result DebugQueryPool::reset()
{
    SLANG_RHI_API_FUNC;
    return baseObject->reset();
}

} // namespace rhi::debug
