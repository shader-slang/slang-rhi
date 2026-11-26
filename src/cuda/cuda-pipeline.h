#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

class ComputePipelineImpl : public ComputePipeline
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    CUmodule m_module = nullptr;
    CUfunction m_function = nullptr;
    std::string m_entryPointName;
    uint32_t m_entryPointIndex = 0;
    uint32_t m_threadGroupSize[3] = {1, 1, 1};
    CUdeviceptr m_globalParams = 0;
    size_t m_globalParamsSize = 0;
    // TODO: This is a temporary flag to warn about global parameter size mismatch once.
    bool m_warnedAboutGlobalParamsSizeMismatch = false;
    size_t m_sharedMemorySize = 0;
    /// Dynamic shared memory per thread group, passed to every launch and to the occupancy query.
    /// Kernels currently only use static shared memory.
    uint32_t m_dynamicSharedMemorySize = 0;
    bool m_cooperative = false;
    uint32_t m_maxCooperativeThreadGroupCount = 0;
    /// Offset of each kernel parameter in the entry point data, for a cooperative launch. The driver
    /// rejects CU_LAUNCH_PARAM_BUFFER_POINTER on a cooperative launch during stream capture, so a
    /// cooperative launch passes one pointer per parameter instead.
    std::vector<size_t> m_kernelParamOffsets;

    ComputePipelineImpl(Device* device, const ComputePipelineDesc& desc);
    ~ComputePipelineImpl();

    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getMaxCooperativeThreadGroupCount(
        IShaderObject* rootObject,
        uint32_t* outCount
    ) override;
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
