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

    std::mutex m_mutex;
    std::map<RayTracingPipelineImpl*, RefPtr<BufferImpl>> m_buffers;

    ShaderTableImpl(Device* device, const ShaderTableDesc& desc);

    BufferImpl* getBuffer(RayTracingPipelineImpl* pipeline);
};

} // namespace rhi::vk
