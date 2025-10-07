#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

class ComputePipelineImpl : public ComputePipeline
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    CUmodule m_module = nullptr;
    CUfunction m_function = nullptr;
    std::string m_kernelName;
    uint32_t m_kernelIndex = 0;
    uint32_t m_threadGroupSize[3] = {1, 1, 1};
    CUdeviceptr m_globalParams = 0;
    size_t m_globalParamsSize = 0;
    size_t m_paramBufferSize = 0;
    size_t m_sharedMemorySize = 0;

    ComputePipelineImpl(Device* device, const ComputePipelineDesc& desc);
    ~ComputePipelineImpl();

    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

#if SLANG_RHI_ENABLE_OPTIX

class RayTracingPipelineImpl : public RayTracingPipeline
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    std::vector<OptixModule> m_modules;
    std::vector<OptixProgramGroup> m_programGroups;
    std::map<std::string, uint32_t> m_shaderGroupNameToIndex;
    OptixPipeline m_pipeline = nullptr;

    RayTracingPipelineImpl(Device* device, const RayTracingPipelineDesc& desc);
    ~RayTracingPipelineImpl();

    // IRayTracingPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

#endif // SLANG_RHI_ENABLE_OPTIX

} // namespace rhi::cuda
