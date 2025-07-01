#pragma once

#include "metal-base.h"

#include <map>
#include <string>

namespace rhi::metal {

class RenderPipelineImpl : public RenderPipeline
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    NS::SharedPtr<MTL::RenderPipelineState> m_pipelineState;
    NS::SharedPtr<MTL::DepthStencilState> m_depthStencilState;
    MTL::PrimitiveType m_primitiveType;
    RasterizerDesc m_rasterizerDesc;
    NS::UInteger m_vertexBufferOffset;

    RenderPipelineImpl(Device* device, const RenderPipelineDesc& desc);

    // IRenderPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class ComputePipelineImpl : public ComputePipeline
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    NS::SharedPtr<MTL::ComputePipelineState> m_pipelineState;
    MTL::Size m_threadGroupSize;

    ComputePipelineImpl(Device* device, const ComputePipelineDesc& desc);

    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class RayTracingPipelineImpl : public RayTracingPipeline
{
public:
    RayTracingPipelineImpl(Device* device, const RayTracingPipelineDesc& desc);

    // IRayTracingPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::metal
