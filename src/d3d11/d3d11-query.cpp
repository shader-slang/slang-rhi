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
    if (!m_queries[index])
        device->m_device->CreateQuery(&m_queryDesc, m_queries[index].writeRef());
    return m_queries[index].get();
}

Result QueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* data)
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;
    while (S_OK !=
           device->m_immediateContext
               ->GetData(device->m_disjointQuery, &disjointData, sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT), 0))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    device->m_info.timestampFrequency = disjointData.Frequency;

    for (uint32_t i = 0; i < count; i++)
    {
        SLANG_RETURN_ON_FAIL(
            device->m_immediateContext->GetData(m_queries[queryIndex + i], data + i, sizeof(uint64_t), 0)
        );
    }
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
