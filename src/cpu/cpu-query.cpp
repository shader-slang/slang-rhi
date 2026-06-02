#include "cpu-query.h"
#include "cpu-device.h"

namespace rhi::cpu {

QueryPoolImpl::QueryPoolImpl(Device* device, const QueryPoolDesc& desc)
    : QueryPool(device, desc)
{
}

Result QueryPoolImpl::isResultReady(uint32_t queryIndex, uint32_t count, bool* outReady)
{
    if (!outReady || !isValidQueryRange(queryIndex, count))
    {
        return SLANG_E_INVALID_ARG;
    }

    *outReady = false;
    QueryRangeInfo queryInfo = getQueryRangeInfo(queryIndex, count);
    if (queryInfo.state == QueryRangeState::Reset)
    {
        return SLANG_FAIL;
    }
    if (queryInfo.state == QueryRangeState::Resolved)
    {
        *outReady = true;
    }
    return SLANG_OK;
}

Result QueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* outData)
{
    if (!outData || !isValidQueryRange(queryIndex, count))
    {
        return SLANG_E_INVALID_ARG;
    }

    if (getQueryRangeInfo(queryIndex, count).state != QueryRangeState::Resolved)
    {
        return SLANG_FAIL;
    }

    for (uint32_t i = 0; i < count; i++)
    {
        outData[i] = m_queries[queryIndex + i];
    }
    return SLANG_OK;
}

Result DeviceImpl::createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool)
{
    RefPtr<QueryPoolImpl> pool = new QueryPoolImpl(this, desc);
    pool->m_queries.resize(desc.count);
    returnComPtr(outPool, pool);
    return SLANG_OK;
}

} // namespace rhi::cpu
