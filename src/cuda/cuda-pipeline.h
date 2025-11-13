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
    // TODO: This is a temporary flag to warn about global parameter size mismatch once.
    bool m_warnedAboutGlobalParamsSizeMismatch = false;
    size_t m_sharedMemorySize = 0;

    ComputePipelineImpl(Device* device, const ComputePipelineDesc& desc);
    ~ComputePipelineImpl();

    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class RayTracingPipelineImpl : public RayTracingPipeline
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    RefPtr<optix::Pipeline> m_optixPipeline;

    RayTracingPipelineImpl(Device* device, const RayTracingPipelineDesc& desc);
    ~RayTracingPipelineImpl();

    // IRayTracingPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::cuda
