#pragma once

#include "metal-base.h"

namespace rhi::metal {

class QueryPoolImpl : public QueryPool
{
public:
    NS::SharedPtr<MTL::CounterSampleBuffer> m_counterSampleBuffer;

    QueryPoolImpl(Device* device, const QueryPoolDesc& desc);
    ~QueryPoolImpl();

    Result init();

    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(uint32_t queryIndex, uint32_t count, uint64_t* data) override;
};

} // namespace rhi::metal
