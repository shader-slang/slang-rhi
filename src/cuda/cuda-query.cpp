#include "cuda-query.h"
#include "cuda-device.h"
#include "cuda-utils.h"

namespace rhi::cuda {

QueryPoolImpl::QueryPoolImpl(Device* device, const QueryPoolDesc& desc)
    : QueryPool(device, desc)
{
}

QueryPoolImpl::~QueryPoolImpl()
{
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

    for (auto& e : m_events)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuEventDestroy(e));
    }
    SLANG_CUDA_ASSERT_ON_FAIL(cuEventDestroy(m_startEvent));
}

Result QueryPoolImpl::init()
{
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuEventCreate(&m_startEvent, 0), this);
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuEventRecord(m_startEvent, 0), this);
    m_events.resize(m_desc.count);
    for (size_t i = 0; i < m_events.size(); i++)
    {
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuEventCreate(&m_events[i], 0), this);
    }
    return SLANG_OK;
}

Result QueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* data)
{
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

    for (uint32_t i = 0; i < count; i++)
    {
        float time = 0.0f;
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuEventSynchronize(m_events[i + queryIndex]), this);
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuEventElapsedTime(&time, m_startEvent, m_events[i + queryIndex]), this);
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
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

    if (m_buffer)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuMemFree(m_buffer));
    }
}

Result PlainBufferProxyQueryPoolImpl::init()
{
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAlloc(&m_buffer, m_desc.count * sizeof(uint64_t)), this);
    return SLANG_OK;
}

Result PlainBufferProxyQueryPoolImpl::reset()
{
    return SLANG_OK;
}

Result PlainBufferProxyQueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* data)
{
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuCtxSynchronize(), this);
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(
        cuMemcpyDtoH(data, m_buffer + queryIndex * sizeof(uint64_t), count * sizeof(uint64_t)),
        this
    );
    return SLANG_OK;
}

} // namespace rhi::cuda
