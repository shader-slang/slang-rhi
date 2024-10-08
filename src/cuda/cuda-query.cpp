#include "cuda-query.h"
#include "cuda-helper-functions.h"

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

Result PlainBufferProxyQueryPoolImpl::init(const QueryPoolDesc& desc, DeviceImpl* device)
{
    m_desc = desc;
    m_device = device;
    SLANG_CUDA_RETURN_ON_FAIL(cuMemAlloc(&m_buffer, desc.count * sizeof(uint64_t)));
    return SLANG_OK;
}

PlainBufferProxyQueryPoolImpl::~PlainBufferProxyQueryPoolImpl()
{
    if (m_buffer)
    {
        cuMemFree(m_buffer);
    }
}

Result PlainBufferProxyQueryPoolImpl::reset()
{
    return SLANG_OK;
}

Result PlainBufferProxyQueryPoolImpl::getResult(GfxIndex queryIndex, GfxCount count, uint64_t* data)
{
    cuCtxSynchronize();
    cuMemcpyDtoH(data, m_buffer + queryIndex * sizeof(uint64_t), count * sizeof(uint64_t));
    return SLANG_OK;
}

} // namespace rhi::cuda
