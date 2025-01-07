#pragma once

#include "cuda-base.h"

#include <map>

#if SLANG_RHI_ENABLE_OPTIX

namespace rhi::cuda {

class ShaderTableImpl : public ShaderTable
{
public:
    struct Instance
    {
        CUdeviceptr buffer;
        OptixShaderBindingTable sbt;
        size_t raygenRecordSize;
    };

    std::map<RayTracingPipelineImpl*, Instance> m_instances;

    Instance* getInstance(RayTracingPipelineImpl* pipeline);

    // TODO - we should probably remove this from the ShaderTable base class
    virtual RefPtr<Buffer> createDeviceBuffer(RayTracingPipeline* pipeline) override;
};

} // namespace rhi::cuda

#endif // SLANG_RHI_ENABLE_OPTIX
