#include "cpu-query.h"

namespace rhi::cpu {

Result QueryPoolImpl::init(const QueryPoolDesc& desc)
{
    m_queries.resize(desc.count);
    return SLANG_OK;
}

SLANG_NO_THROW Result SLANG_MCALL QueryPoolImpl::getResult(GfxIndex queryIndex, GfxCount count, uint64_t* data)
{
    for (GfxCount i = 0; i < count; i++)
    {
        data[i] = m_queries[queryIndex + i];
    }
    return SLANG_OK;
}

} // namespace rhi::cpu
