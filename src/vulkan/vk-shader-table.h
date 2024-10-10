#pragma once

#include "vk-base.h"

namespace rhi::vk {

class ShaderTableImpl : public ShaderTable
{
public:
    uint32_t m_raygenTableSize;
    uint32_t m_missTableSize;
    uint32_t m_hitTableSize;
    uint32_t m_callableTableSize;

    DeviceImpl* m_device;

    virtual RefPtr<Buffer> createDeviceBuffer(
        RayTracingPipeline* pipeline,
        TransientResourceHeap* transientHeap,
        IRayTracingPassEncoder* encoder
    ) override;
};

} // namespace rhi::vk
