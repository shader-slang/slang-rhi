#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class QueryPoolImpl : public QueryPool
{
public:
    struct Query
    {
        ComPtr<ID3D11Query> timestampQuery;
        ComPtr<ID3D11Query> disjointQuery;
    };

    std::vector<Query> m_queries;
    D3D11_QUERY_DESC m_queryDesc;

    QueryPoolImpl(Device* device, const QueryPoolDesc& desc);

    Result init();

    ID3D11Query* getQuery(uint32_t index);
    void setDisjointQuery(uint32_t index, ID3D11Query* disjointQuery);

    virtual SLANG_NO_THROW Result SLANG_MCALL isResultReady(
        uint32_t queryIndex,
        uint32_t count,
        bool* outReady
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(
        uint32_t queryIndex,
        uint32_t count,
        uint64_t* outData
    ) override;
};

} // namespace rhi::d3d11
