#include "cuda-query.h"
#include "cuda-command.h"
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

    for (Query& query : m_queries)
    {
        if (query.event)
        {
            SLANG_CUDA_ASSERT_ON_FAIL(cuEventDestroy(query.event));
        }
    }
}

Result QueryPoolImpl::init()
{
    m_queries.resize(m_desc.count);
    for (Query& query : m_queries)
    {
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuEventCreate(&query.event, 0), this);
    }
    return SLANG_OK;
}

Result QueryPoolImpl::reset()
{
    return reset(0, m_desc.count);
}

Result QueryPoolImpl::reset(uint32_t queryIndex, uint32_t count)
{
    SLANG_RETURN_ON_FAIL(QueryPool::reset(queryIndex, count));
    for (uint32_t i = 0; i < count; ++i)
    {
        Query& query = m_queries[queryIndex + i];
        query.anchorGeneration = kInvalidTimestampAnchorGeneration;
        query.resultData = 0;
    }
    return SLANG_OK;
}

Result QueryPoolImpl::getResultState(uint32_t queryIndex, uint32_t count, QueryResultState* outState)
{
    if (!outState || !isValidQueryRange(queryIndex, count))
    {
        return SLANG_E_INVALID_ARG;
    }

    QueryRangeInfo queryInfo = getQueryRangeInfo(queryIndex, count);
    if (queryInfo.state == QueryResultState::Reset)
    {
        *outState = QueryResultState::Reset;
        return SLANG_OK;
    }
    if (queryInfo.state == QueryResultState::Resolved)
    {
        *outState = QueryResultState::Resolved;
        return SLANG_OK;
    }

    CommandQueueImpl* queue = getDevice<DeviceImpl>()->m_queue.get();
    SLANG_RETURN_ON_FAIL(queue->retireCommandBuffers());

    queryInfo = getQueryRangeInfo(queryIndex, count);
    if (queryInfo.state == QueryResultState::Reset)
    {
        *outState = QueryResultState::Reset;
        return SLANG_OK;
    }
    if (queryInfo.state == QueryResultState::Resolved)
    {
        *outState = QueryResultState::Resolved;
        return SLANG_OK;
    }
    if (queue->m_lastFinishedID < queryInfo.submissionID)
    {
        *outState = QueryResultState::Pending;
        return SLANG_OK;
    }

    return SLANG_FAIL;
}

Result QueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* outData)
{
    if (!outData || !isValidQueryRange(queryIndex, count))
    {
        return SLANG_E_INVALID_ARG;
    }
    QueryRangeInfo queryInfo = getQueryRangeInfo(queryIndex, count);
    if (queryInfo.state == QueryResultState::Reset)
    {
        return SLANG_FAIL;
    }
    if (count == 0)
    {
        return SLANG_OK;
    }

    if (queryInfo.state != QueryResultState::Resolved)
    {
        CommandQueueImpl* queue = getDevice<DeviceImpl>()->m_queue.get();
        SLANG_RETURN_ON_FAIL(queue->waitOnHost());
        queryInfo = getQueryRangeInfo(queryIndex, count);
        if (queryInfo.state != QueryResultState::Resolved)
        {
            return SLANG_FAIL;
        }
    }

    for (uint32_t i = 0; i < count; i++)
    {
        outData[i] = m_queries[queryIndex + i].resultData;
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
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAlloc(&m_buffer, m_desc.count * sizeof(uint64_t)), this);
    return SLANG_OK;
}

Result PlainBufferProxyQueryPoolImpl::reset()
{
    SLANG_RETURN_ON_FAIL(QueryPool::reset());
    return SLANG_OK;
}

Result PlainBufferProxyQueryPoolImpl::getResultState(uint32_t queryIndex, uint32_t count, QueryResultState* outState)
{
    if (!outState || !isValidQueryRange(queryIndex, count))
    {
        return SLANG_E_INVALID_ARG;
    }

    QueryRangeInfo queryInfo = getQueryRangeInfo(queryIndex, count);
    if (queryInfo.state == QueryResultState::Reset)
    {
        *outState = QueryResultState::Reset;
        return SLANG_OK;
    }
    if (queryInfo.state == QueryResultState::Resolved)
    {
        *outState = QueryResultState::Resolved;
        return SLANG_OK;
    }

    CommandQueueImpl* queue = getDevice<DeviceImpl>()->m_queue.get();
    SLANG_RETURN_ON_FAIL(queue->retireCommandBuffers());
    uint64_t submissionID = queryInfo.submissionID;
    if (queue->m_lastFinishedID < submissionID)
    {
        *outState = QueryResultState::Pending;
        return SLANG_OK;
    }

    markQueryRangeResolved(queryIndex, count, queryInfo.submissionID);
    *outState = QueryResultState::Resolved;

    return SLANG_OK;
}

Result PlainBufferProxyQueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* outData)
{
    if (!outData || !isValidQueryRange(queryIndex, count))
    {
        return SLANG_E_INVALID_ARG;
    }
    QueryRangeInfo queryInfo = getQueryRangeInfo(queryIndex, count);
    if (queryInfo.state == QueryResultState::Reset)
    {
        return SLANG_FAIL;
    }
    if (count == 0)
    {
        return SLANG_OK;
    }

    CommandQueueImpl* queue = getDevice<DeviceImpl>()->m_queue.get();
    if (queue->m_lastFinishedID < queryInfo.submissionID)
    {
        SLANG_RETURN_ON_FAIL(queue->waitOnHost());
    }
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(
        cuMemcpyDtoH(outData, m_buffer + queryIndex * sizeof(uint64_t), count * sizeof(uint64_t)),
        this
    );

    markQueryRangeResolved(queryIndex, count, queryInfo.submissionID);

    return SLANG_OK;
}

} // namespace rhi::cuda
