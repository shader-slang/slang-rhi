#include "cpu-query.h"
#include "cpu-device.h"

namespace rhi::cpu {

Result QueryPoolImpl::getResult(GfxIndex queryIndex, GfxCount count, uint64_t* data)
{
    for (GfxCount i = 0; i < count; i++)
    {
        data[i] = m_queries[queryIndex + i];
    }
    return SLANG_OK;
}

Result DeviceImpl::createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool)
{
    RefPtr<QueryPoolImpl> pool = new QueryPoolImpl();
    pool->m_queries.resize(desc.count);
    returnComPtr(outPool, pool);
    return SLANG_OK;
}

} // namespace rhi::cpu
