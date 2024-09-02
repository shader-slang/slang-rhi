#pragma once

#include "metal-base.h"

#include <map>
#include <string>

namespace rhi::metal {

class RenderPipelineImpl : public RenderPipelineBase
{
public:
    RenderPipelineImpl(DeviceImpl* device);
    virtual ~RenderPipelineImpl() override;
    Result init(const RenderPipelineDesc& desc);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    RefPtr<DeviceImpl> m_device;
    RasterizerDesc m_rasterizerDesc;
    DepthStencilDesc m_depthStencilDesc;
    NS::SharedPtr<MTL::RenderPipelineState> m_renderPipelineState;
    NS::SharedPtr<MTL::DepthStencilState> m_depthStencilState;
    NS::UInteger m_vertexBufferOffset;
};

class ComputePipelineImpl : public ComputePipelineBase
{
public:
    ComputePipelineImpl(DeviceImpl* device);
    virtual ~ComputePipelineImpl() override;
    Result init(const ComputePipelineDesc& desc);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    RefPtr<DeviceImpl> m_device;
    NS::SharedPtr<MTL::ComputePipelineState> m_computePipelineState;
    MTL::Size m_threadGroupSize;
};

} // namespace rhi::metal
