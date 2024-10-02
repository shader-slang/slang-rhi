#pragma once

#include "metal-base.h"

namespace rhi::metal {

class ShaderTableImpl : public ShaderTable
{
public:
    uint32_t m_raygenTableSize;
    uint32_t m_missTableSize;
    uint32_t m_hitTableSize;
    uint32_t m_callableTableSize;

    DeviceImpl* m_device;

    virtual RefPtr<Buffer> createDeviceBuffer(
        Pipeline* pipeline,
        TransientResourceHeap* transientHeap,
        IRayTracingPassEncoder* encoder
    ) override;
};

} // namespace rhi::metal
