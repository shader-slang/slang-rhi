#pragma once

#include "metal-base.h"

namespace rhi::metal {

class QueryPoolImpl : public QueryPool
{
public:
    NS::SharedPtr<MTL::CounterSampleBuffer> m_counterSampleBuffer;
    NS::SharedPtr<MTL::Buffer> m_readbackBuffer;

    QueryPoolImpl(Device* device, const QueryPoolDesc& desc);
    ~QueryPoolImpl();

    Result init();

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

MTL::CounterSet* findTimestampCounterSet(MTL::Device* device);
bool supportsTimestampQueries(MTL::Device* device);

} // namespace rhi::metal
