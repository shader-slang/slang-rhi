#pragma once

#include "cuda-base.h"

#include <map>

namespace rhi::cuda {

class ShaderTableImpl : public ShaderTable
{
public:
    ShaderTableImpl(Device* device, const ShaderTableDesc& desc);
    ~ShaderTableImpl();

    std::mutex m_mutex;
    std::map<RayTracingPipelineImpl*, RefPtr<optix::ShaderBindingTable>> m_shaderBindingTables;

    optix::ShaderBindingTable* getShaderBindingTable(RayTracingPipelineImpl* pipeline);
};

} // namespace rhi::cuda
