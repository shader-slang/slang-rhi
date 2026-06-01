#include "d3d11-query.h"
#include "d3d11-device.h"

#include <thread>
#include <chrono>

namespace rhi::d3d11 {

QueryPoolImpl::QueryPoolImpl(Device* device, const QueryPoolDesc& desc)
    : QueryPool(device, desc)
{
}

Result QueryPoolImpl::init()
{
    m_queryDesc.MiscFlags = 0;
    switch (m_desc.type)
    {
    case QueryType::Timestamp:
        m_queryDesc.Query = D3D11_QUERY_TIMESTAMP;
        break;
    default:
        return SLANG_E_INVALID_ARG;
    }
    m_queries.resize(m_desc.count);
    return SLANG_OK;
}

ID3D11Query* QueryPoolImpl::getQuery(uint32_t index)
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    if (!m_queries[index].timestampQuery)
        device->m_device->CreateQuery(&m_queryDesc, m_queries[index].timestampQuery.writeRef());
    return m_queries[index].timestampQuery.get();
}

void QueryPoolImpl::setDisjointQuery(uint32_t index, ID3D11Query* disjointQuery)
{
    SLANG_RHI_ASSERT(index < m_queries.size());
    m_queries[index].disjointQuery = disjointQuery;
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
        return SLANG_OK;
    }

    DeviceImpl* device = getDevice<DeviceImpl>();
    for (uint32_t i = 0; i < count; i++)
    {
        const Query& query = m_queries[queryIndex + i];
        if (!query.timestampQuery || !query.disjointQuery)
        {
            return SLANG_FAIL;
        }

        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData = {};
        HRESULT hr =
            device->m_immediateContext
                ->GetData(query.disjointQuery, &disjointData, sizeof(disjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (hr == S_FALSE)
        {
            return SLANG_OK;
        }
        SLANG_RETURN_ON_FAIL(hr);
        if (disjointData.Disjoint || disjointData.Frequency == 0)
        {
            return SLANG_FAIL;
        }
        device->m_info.timestampFrequency = disjointData.Frequency;

        uint64_t value = 0;
        hr = device->m_immediateContext
                 ->GetData(query.timestampQuery, &value, sizeof(value), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (hr == S_FALSE)
        {
            return SLANG_OK;
        }
        SLANG_RETURN_ON_FAIL(hr);
    }

    markQueryRangeReady(queryIndex, count, queryInfo.submissionID);
    *outReady = true;

    return SLANG_OK;
}

Result QueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* outData)
{
    if (!outData || !isValidQueryRange(queryIndex, count))
    {
        return SLANG_E_INVALID_ARG;
    }

    QueryRangeInfo queryInfo = getQueryRangeInfo(queryIndex, count);
    if (queryInfo.state == QueryRangeState::Reset)
    {
        return SLANG_FAIL;
    }
    if (count == 0)
    {
        return SLANG_OK;
    }

    DeviceImpl* device = getDevice<DeviceImpl>();
    for (uint32_t i = 0; i < count; i++)
    {
        const Query& query = m_queries[queryIndex + i];
        if (!query.timestampQuery || !query.disjointQuery)
        {
            return SLANG_FAIL;
        }

        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData = {};
        HRESULT hr = S_FALSE;
        while (
            (hr = device->m_immediateContext
                      ->GetData(query.disjointQuery, &disjointData, sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT), 0)) ==
            S_FALSE
        )
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        SLANG_RETURN_ON_FAIL(hr);
        if (disjointData.Disjoint || disjointData.Frequency == 0)
        {
            return SLANG_FAIL;
        }
        device->m_info.timestampFrequency = disjointData.Frequency;

        while ((hr = device->m_immediateContext->GetData(query.timestampQuery, outData + i, sizeof(uint64_t), 0)) ==
               S_FALSE)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        SLANG_RETURN_ON_FAIL(hr);
    }

    markQueryRangeReady(queryIndex, count, queryInfo.submissionID);

    return SLANG_OK;
}

Result DeviceImpl::createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool)
{
    RefPtr<QueryPoolImpl> result = new QueryPoolImpl(this, desc);
    SLANG_RETURN_ON_FAIL(result->init());
    returnComPtr(outPool, result);
    return SLANG_OK;
}

} // namespace rhi::d3d11
