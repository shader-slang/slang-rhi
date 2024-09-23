#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class QueryPoolImpl : public QueryPool
{
public:
    RefPtr<DeviceImpl> m_device;
    WGPUQuerySet m_querySet = nullptr;

    ~QueryPoolImpl();

    // IQueryPool implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(GfxIndex index, GfxCount count, uint64_t* data) override;
};

} // namespace rhi::wgpu
