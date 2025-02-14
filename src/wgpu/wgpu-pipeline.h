#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class RenderPipelineImpl : public RenderPipeline
{
public:
    DeviceImpl* m_device;
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    WGPURenderPipeline m_renderPipeline = nullptr;

    ~RenderPipelineImpl();

    // IRenderPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class ComputePipelineImpl : public ComputePipeline
{
public:
    DeviceImpl* m_device;
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    WGPUComputePipeline m_computePipeline = nullptr;

    ~ComputePipelineImpl();

    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::wgpu
