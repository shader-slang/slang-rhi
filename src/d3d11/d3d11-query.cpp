#include "d3d11-query.h"
#include "d3d11-device.h"

#include <thread>
#include <chrono>

namespace rhi::d3d11 {

Result QueryPoolImpl::init(const QueryPoolDesc& desc, DeviceImpl* device)
{
    m_device = device;
    m_queryDesc.MiscFlags = 0;
    switch (desc.type)
    {
    case QueryType::Timestamp:
        m_queryDesc.Query = D3D11_QUERY_TIMESTAMP;
        break;
    default:
        return SLANG_E_INVALID_ARG;
    }
    m_queries.resize(desc.count);
    return SLANG_OK;
}

ID3D11Query* QueryPoolImpl::getQuery(SlangInt index)
{
    if (!m_queries[index])
        m_device->m_device->CreateQuery(&m_queryDesc, m_queries[index].writeRef());
    return m_queries[index].get();
}

Result QueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* data)
{
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;
    while (S_OK !=
           m_device->m_immediateContext
               ->GetData(m_device->m_disjointQuery, &disjointData, sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT), 0))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    m_device->m_info.timestampFrequency = disjointData.Frequency;

    for (SlangInt i = 0; i < count; i++)
    {
        SLANG_RETURN_ON_FAIL(
            m_device->m_immediateContext->GetData(m_queries[queryIndex + i], data + i, sizeof(uint64_t), 0)
        );
    }
    return SLANG_OK;
}

Result DeviceImpl::createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool)
{
    RefPtr<QueryPoolImpl> result = new QueryPoolImpl();
    SLANG_RETURN_ON_FAIL(result->init(desc, this));
    returnComPtr(outPool, result);
    return SLANG_OK;
}

} // namespace rhi::d3d11
