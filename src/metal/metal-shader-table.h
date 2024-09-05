#pragma once

#include "metal-base.h"

namespace rhi::metal {

class ShaderTableImpl : public ShaderTableBase
{
public:
    uint32_t m_raygenTableSize;
    uint32_t m_missTableSize;
    uint32_t m_hitTableSize;
    uint32_t m_callableTableSize;

    DeviceImpl* m_device;

    virtual RefPtr<Buffer> createDeviceBuffer(
        PipelineBase* pipeline,
        TransientResourceHeapBase* transientHeap,
        IRayTracingCommandEncoder* encoder
    ) override;
};

} // namespace rhi::metal
