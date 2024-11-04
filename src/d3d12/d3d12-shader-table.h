#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class ShaderTableImpl : public ShaderTable
{
public:
    uint32_t m_rayGenTableOffset;
    uint32_t m_missTableOffset;
    uint32_t m_hitGroupTableOffset;
    uint32_t m_callableTableOffset;

    DeviceImpl* m_device;

    virtual RefPtr<Buffer> createDeviceBuffer(RayTracingPipeline* pipeline) override;
};

} // namespace rhi::d3d12
