#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class ShaderTableImpl : public ShaderTableBase
{
public:
    uint32_t m_rayGenTableOffset;
    uint32_t m_missTableOffset;
    uint32_t m_hitGroupTableOffset;
    uint32_t m_callableTableOffset;

    DeviceImpl* m_device;

    virtual RefPtr<Buffer> createDeviceBuffer(
        PipelineStateBase* pipeline,
        TransientResourceHeapBase* transientHeap,
        IResourceCommandEncoder* encoder
    ) override;
};

} // namespace rhi::d3d12
