#include "debug-query.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

Result DebugQueryPool::getResult(uint32_t queryIndex, uint32_t count, uint64_t* data)
{
    SLANG_RHI_API_FUNC;

    if (queryIndex + count > desc.count)
        RHI_VALIDATION_ERROR("index is out of bounds.");
    return baseObject->getResult(queryIndex, count, data);
}

Result DebugQueryPool::reset()
{
    SLANG_RHI_API_FUNC;
    return baseObject->reset();
}

} // namespace rhi::debug
