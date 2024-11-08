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

    // IRenderPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class ComputePipelineImpl : public ComputePipeline
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    NS::SharedPtr<MTL::ComputePipelineState> m_pipelineState;
    MTL::Size m_threadGroupSize;

    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class RayTracingPipelineImpl : public RayTracingPipeline
{
public:
    // IRayTracingPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::metal
