#pragma once

#include "cuda-base.h"

#include <map>

namespace rhi::cuda {

class ShaderTableImpl : public ShaderTable
{
public:
    ShaderTableImpl(Device* device, const ShaderTableDesc& desc);
    ~ShaderTableImpl();

    struct Instance
    {
        RefPtr<optix::ShaderBindingTable> optixShaderBindingTable;
    };

    std::mutex m_mutex;
    std::map<RayTracingPipelineImpl*, Instance> m_instances;

    Instance* getInstance(RayTracingPipelineImpl* pipeline);
};

} // namespace rhi::cuda
