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

    uint32_t m_rayGenRecordStride;
    uint32_t m_missRecordStride;
    uint32_t m_hitGroupRecordStride;
    uint32_t m_callableRecordStride;

    std::mutex m_mutex;
    std::map<RayTracingPipelineImpl*, RefPtr<BufferImpl>> m_buffers;

    ShaderTableImpl(Device* device, const ShaderTableDesc& desc);

    BufferImpl* getBuffer(RayTracingPipelineImpl* pipeline);
};

} // namespace rhi::d3d12
