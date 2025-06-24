#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class RenderPipelineImpl : public RenderPipeline
{
public:
    RefPtr<InputLayoutImpl> m_inputLayout;
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    D3D_PRIMITIVE_TOPOLOGY m_primitiveTopology;

    RenderPipelineImpl(Device* device, const RenderPipelineDesc& desc);

    // IRenderPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class ComputePipelineImpl : public ComputePipeline
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    ComPtr<ID3D12PipelineState> m_pipelineState;

    ComputePipelineImpl(Device* device, const ComputePipelineDesc& desc);

    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class RayTracingPipelineImpl : public RayTracingPipeline
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    ComPtr<ID3D12StateObject> m_stateObject;

    RayTracingPipelineImpl(Device* device, const RayTracingPipelineDesc& desc);

    // IRayTracingPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::d3d12
