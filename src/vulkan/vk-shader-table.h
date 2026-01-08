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

    uint32_t m_raygenRecordStride;
    uint32_t m_missRecordStride;
    uint32_t m_hitGroupRecordStride;
    uint32_t m_callableRecordStride;

    std::mutex m_mutex;
    std::map<RayTracingPipelineImpl*, RefPtr<BufferImpl>> m_buffers;

    ShaderTableImpl(Device* device, const ShaderTableDesc& desc);

    BufferImpl* getBuffer(RayTracingPipelineImpl* pipeline);
};

} // namespace rhi::vk
