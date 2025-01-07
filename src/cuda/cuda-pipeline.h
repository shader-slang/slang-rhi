#pragma once

#include "cuda-base.h"
#include "cuda-shader-program.h"

namespace rhi::cuda {

class ComputePipelineImpl : public ComputePipeline
{
public:
    RefPtr<DeviceImpl> m_device;
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    CUmodule m_module = nullptr;
    CUfunction m_function = nullptr;
    std::string m_kernelName;

    ~ComputePipelineImpl();

    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

#if SLANG_RHI_ENABLE_OPTIX

class RayTracingPipelineImpl : public RayTracingPipeline
{
public:
    RefPtr<DeviceImpl> m_device;
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    std::vector<OptixModule> m_modules;
    std::vector<OptixProgramGroup> m_programGroups;
    std::map<std::string, Index> m_shaderGroupNameToIndex;
    OptixPipeline m_pipeline = nullptr;

    ~RayTracingPipelineImpl();

    // IRayTracingPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

#endif // SLANG_RHI_ENABLE_OPTIX

} // namespace rhi::cuda
