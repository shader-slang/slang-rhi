#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class RenderPipelineImpl : public RenderPipeline
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    WGPURenderPipeline m_renderPipeline = nullptr;

    RenderPipelineImpl(Device* device, const RenderPipelineDesc& desc);
    ~RenderPipelineImpl();

    // IRenderPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class ComputePipelineImpl : public ComputePipeline
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    WGPUComputePipeline m_computePipeline = nullptr;

    ComputePipelineImpl(Device* device, const ComputePipelineDesc& desc);
    ~ComputePipelineImpl();

    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::wgpu
