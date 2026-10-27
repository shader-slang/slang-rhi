#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

/// Pipelines created on this thread while a scope is alive use the synchronous entry points instead
/// of waiting on the async ones. finish() resolves deferred pipelines in a scope, since it must not
/// wait: in browsers, waiting yields between acquiring a canvas texture and submitting.
class PipelineResolutionScope
{
public:
    PipelineResolutionScope() { s_active = true; }
    ~PipelineResolutionScope() { s_active = false; }
    static bool isActive() { return s_active; }

private:
    static inline thread_local bool s_active = false;
};

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
