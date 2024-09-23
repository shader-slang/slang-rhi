#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class PipelineImpl : public Pipeline
{
public:
    DeviceImpl* m_device;
    WGPURenderPipeline m_renderPipeline = nullptr;
    WGPUComputePipeline m_computePipeline = nullptr;

    ~PipelineImpl();

    void init(const RenderPipelineDesc& desc);
    void init(const ComputePipelineDesc& desc);
    void init(const RayTracingPipelineDesc& desc);

    Result createRenderPipeline();
    Result createComputePipeline();

    virtual Result ensureAPIPipelineCreated() override;

    // IPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::wgpu
