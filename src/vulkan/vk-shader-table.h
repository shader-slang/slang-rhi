#pragma once

#include "vk-base.h"

namespace rhi::vk {

class ShaderTableImpl : public ShaderTable
{
public:
    RefPtr<DeviceImpl> m_device;

    uint32_t m_raygenTableSize;
    uint32_t m_missTableSize;
    uint32_t m_hitTableSize;
    uint32_t m_callableTableSize;

    virtual RefPtr<Buffer> createDeviceBuffer(RayTracingPipeline* pipeline) override;
};

} // namespace rhi::vk
