#include "cuda-query.h"
#include "cuda-helper-functions.h"

namespace rhi::cuda {

QueryPoolImpl::QueryPoolImpl(Device* device, const QueryPoolDesc& desc)
    : QueryPool(device, desc)
{
}

QueryPoolImpl::~QueryPoolImpl()
{
    for (auto& e : m_events)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuEventDestroy(e));
    }
    SLANG_CUDA_ASSERT_ON_FAIL(cuEventDestroy(m_startEvent));
}

Result QueryPoolImpl::init()
{
    SLANG_CUDA_RETURN_ON_FAIL(cuEventCreate(&m_startEvent, 0));
    SLANG_CUDA_RETURN_ON_FAIL(cuEventRecord(m_startEvent, 0));
    m_events.resize(m_desc.count);
    for (size_t i = 0; i < m_events.size(); i++)
    {
        SLANG_CUDA_RETURN_ON_FAIL(cuEventCreate(&m_events[i], 0));
    }
    return SLANG_OK;
}

Result QueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* data)
{
    for (uint32_t i = 0; i < count; i++)
    {
        float time = 0.0f;
        SLANG_CUDA_RETURN_ON_FAIL(cuEventSynchronize(m_events[i + queryIndex]));
        SLANG_CUDA_RETURN_ON_FAIL(cuEventElapsedTime(&time, m_startEvent, m_events[i + queryIndex]));
        data[i] = (uint64_t)((double)time * 1000.0f);
    }
    return SLANG_OK;
}

PlainBufferProxyQueryPoolImpl::PlainBufferProxyQueryPoolImpl(Device* device, const QueryPoolDesc& desc)
    : QueryPool(device, desc)
{
}

PlainBufferProxyQueryPoolImpl::~PlainBufferProxyQueryPoolImpl()
{
    if (m_buffer)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuMemFree(m_buffer));
    }
}

Result PlainBufferProxyQueryPoolImpl::init()
{
    SLANG_CUDA_RETURN_ON_FAIL(cuMemAlloc(&m_buffer, m_desc.count * sizeof(uint64_t)));
    return SLANG_OK;
}

Result PlainBufferProxyQueryPoolImpl::reset()
{
    return SLANG_OK;
}

Result PlainBufferProxyQueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* data)
{
    SLANG_CUDA_RETURN_ON_FAIL(cuCtxSynchronize());
    SLANG_CUDA_RETURN_ON_FAIL(cuMemcpyDtoH(data, m_buffer + queryIndex * sizeof(uint64_t), count * sizeof(uint64_t)));
    return SLANG_OK;
}

} // namespace rhi::cuda
