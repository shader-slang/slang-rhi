#pragma once

#include "webgpu-base.h"

namespace rhi::webgpu {

class QueryPoolImpl : public QueryPool
{
public:
    WebGPUQuerySet m_querySet = nullptr;

    QueryPoolImpl(Device* device, const QueryPoolDesc& desc);
    ~QueryPoolImpl();

    // IQueryPool implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(uint32_t queryIndex, uint32_t count, uint64_t* data) override;
};

} // namespace rhi::webgpu
