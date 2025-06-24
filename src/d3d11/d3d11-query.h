#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class QueryPoolImpl : public QueryPool
{
public:
    std::vector<ComPtr<ID3D11Query>> m_queries;
    D3D11_QUERY_DESC m_queryDesc;

    QueryPoolImpl(Device* device, const QueryPoolDesc& desc);

    Result init();

    ID3D11Query* getQuery(uint32_t index);

    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(uint32_t queryIndex, uint32_t count, uint64_t* data) override;
};

} // namespace rhi::d3d11
