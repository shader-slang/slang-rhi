#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

class QueryPoolImpl : public QueryPool
{
public:
    /// The event object for each query. Owned by the pool.
    std::vector<CUevent> m_events;

    /// The event that marks the starting point.
    CUevent m_startEvent;

    QueryPoolImpl(Device* device, const QueryPoolDesc& desc);
    ~QueryPoolImpl();

    Result init();

    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(uint32_t queryIndex, uint32_t count, uint64_t* data) override;
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
    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(uint32_t queryIndex, uint32_t count, uint64_t* data) override;
};

} // namespace rhi::cuda
