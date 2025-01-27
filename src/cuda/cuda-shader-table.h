#pragma once

#include "cuda-base.h"

#include <map>

#if SLANG_RHI_ENABLE_OPTIX

namespace rhi::cuda {

class ShaderTableImpl : public ShaderTable
{
public:
    ~ShaderTableImpl();

    struct Instance
    {
        CUdeviceptr buffer;
        OptixShaderBindingTable sbt;
        size_t raygenRecordSize;
    };

    std::mutex m_mutex;
    std::map<RayTracingPipelineImpl*, Instance> m_instances;

    Instance* getInstance(RayTracingPipelineImpl* pipeline);
};

} // namespace rhi::cuda

#endif // SLANG_RHI_ENABLE_OPTIX
