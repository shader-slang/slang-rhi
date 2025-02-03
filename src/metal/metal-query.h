#pragma once

#include "metal-base.h"

namespace rhi::metal {

class QueryPoolImpl : public QueryPool
{
public:
    RefPtr<DeviceImpl> m_device;
    NS::SharedPtr<MTL::CounterSampleBuffer> m_counterSampleBuffer;

    ~QueryPoolImpl();

    Result init(DeviceImpl* device, const QueryPoolDesc& desc);

    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(uint32_t queryIndex, uint32_t count, uint64_t* data) override;
};

} // namespace rhi::metal
