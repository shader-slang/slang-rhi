#include "debug-query.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

const QueryPoolDesc& DebugQueryPool::getDesc()
{
    SLANG_RHI_DEBUG_API(IQueryPool, getDesc);

    return baseObject->getDesc();
}

Result DebugQueryPool::getResult(uint32_t queryIndex, uint32_t count, uint64_t* outData)
{
    SLANG_RHI_DEBUG_API(IQueryPool, getResult);

    if (!outData)
    {
        RHI_VALIDATION_ERROR("'outData' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (!isValidSubrange(queryIndex, count, baseObject->getDesc().count))
    {
        RHI_VALIDATION_ERROR("'queryIndex' and 'count' must specify a valid range within the query pool.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->getResult(queryIndex, count, outData);
}

Result DebugQueryPool::reset()
{
    SLANG_RHI_DEBUG_API(IQueryPool, reset);

    return baseObject->reset();
}

} // namespace rhi::debug
