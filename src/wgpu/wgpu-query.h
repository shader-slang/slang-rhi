#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class QueryPoolImpl : public QueryPool
{
public:
    WGPUQuerySet m_querySet = nullptr;

    QueryPoolImpl(Device* device, const QueryPoolDesc& desc);
    ~QueryPoolImpl();

    // IQueryPool implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(uint32_t queryIndex, uint32_t count, uint64_t* data) override;
};

} // namespace rhi::wgpu
