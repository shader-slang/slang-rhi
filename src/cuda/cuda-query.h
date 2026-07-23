#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

class QueryPoolImpl : public QueryPool
{
public:
    struct Query
    {
        /// The event object for this query. Owned by the pool.
        CUevent event = nullptr;
        /// The queue timestamp anchor generation that was current when this query event was recorded.
        uint64_t anchorGeneration = kInvalidTimestampAnchorGeneration;
        /// Cached host-readable timestamp result, resolved when queue progress retires the command buffer.
        uint64_t resultData = 0;
    };

    std::vector<Query> m_queries;

    QueryPoolImpl(Device* device, const QueryPoolDesc& desc);
    ~QueryPoolImpl();

    Result init();

    virtual SLANG_NO_THROW Result SLANG_MCALL reset() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL reset(uint32_t queryIndex, uint32_t count) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getResultState(
        uint32_t queryIndex,
        uint32_t count,
        QueryResultState* outState
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(
        uint32_t queryIndex,
        uint32_t count,
        uint64_t* outData
    ) override;
};

/// Implements the IQueryPool interface with a plain buffer.
/// Used for query types that does not correspond to a CUDA query,
/// such as ray-tracing acceleration structure post-build info.
class PlainBufferProxyQueryPoolImpl : public QueryPool
{
public:
    CUdeviceptr m_buffer;

    PlainBufferProxyQueryPoolImpl(Device* device, const QueryPoolDesc& desc);
    ~PlainBufferProxyQueryPoolImpl();

    Result init();

    virtual SLANG_NO_THROW Result SLANG_MCALL reset() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getResultState(
        uint32_t queryIndex,
        uint32_t count,
        QueryResultState* outState
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(
        uint32_t queryIndex,
        uint32_t count,
        uint64_t* outData
    ) override;
};

} // namespace rhi::cuda
