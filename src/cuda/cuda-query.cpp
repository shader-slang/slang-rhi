#include "cuda-query.h"

namespace rhi::cuda {

Result QueryPoolImpl::init(const QueryPoolDesc& desc)
{
    cuEventCreate(&m_startEvent, 0);
    cuEventRecord(m_startEvent, 0);
    m_events.resize(desc.count);
    for (SlangInt i = 0; i < m_events.size(); i++)
    {
        cuEventCreate(&m_events[i], 0);
    }
    return SLANG_OK;
}

QueryPoolImpl::~QueryPoolImpl()
{
    for (auto& e : m_events)
    {
        cuEventDestroy(e);
    }
    cuEventDestroy(m_startEvent);
}

Result QueryPoolImpl::getResult(GfxIndex queryIndex, GfxCount count, uint64_t* data)
{
    for (GfxIndex i = 0; i < count; i++)
    {
        float time = 0.0f;
        cuEventSynchronize(m_events[i + queryIndex]);
        cuEventElapsedTime(&time, m_startEvent, m_events[i + queryIndex]);
        data[i] = (uint64_t)((double)time * 1000.0f);
    }
    return SLANG_OK;
}

} // namespace rhi::cuda
