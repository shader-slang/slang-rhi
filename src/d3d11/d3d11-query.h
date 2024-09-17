#pragma once

#include "d3d11-base.h"
#include "d3d11-device.h"

namespace rhi::d3d11 {

class QueryPoolImpl : public QueryPool
{
public:
    std::vector<ComPtr<ID3D11Query>> m_queries;
    RefPtr<DeviceImpl> m_device;
    D3D11_QUERY_DESC m_queryDesc;

    Result init(const QueryPoolDesc& desc, DeviceImpl* device);
    ID3D11Query* getQuery(SlangInt index);
    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(GfxIndex queryIndex, GfxCount count, uint64_t* data) override;
};

} // namespace rhi::d3d11
