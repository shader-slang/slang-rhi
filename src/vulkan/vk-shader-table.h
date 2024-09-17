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
        Pipeline* pipeline,
        TransientResourceHeap* transientHeap,
        IRayTracingCommandEncoder* encoder
    ) override;
};

} // namespace rhi::vk
