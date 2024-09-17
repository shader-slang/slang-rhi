#pragma once

#include "metal-base.h"

#include <map>
#include <string>

namespace rhi::metal {

class PipelineImpl : public Pipeline
{
public:
    DeviceImpl* m_device;
    NS::SharedPtr<MTL::RenderPipelineState> m_renderPipelineState;
    NS::SharedPtr<MTL::DepthStencilState> m_depthStencilState;
    NS::SharedPtr<MTL::ComputePipelineState> m_computePipelineState;
    MTL::Size m_threadGroupSize;
    NS::UInteger m_vertexBufferOffset;

    PipelineImpl(DeviceImpl* device);
    ~PipelineImpl();

    void init(const RenderPipelineDesc& desc);
    void init(const ComputePipelineDesc& desc);
    void init(const RayTracingPipelineDesc& desc);

    Result createMetalComputePipelineState();
    Result createMetalRenderPipelineState();

    virtual Result ensureAPIPipelineCreated() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class RayTracingPipelineImpl : public PipelineImpl
{
public:
    std::map<std::string, Index> shaderGroupNameToIndex;
    Int shaderGroupCount;

    RayTracingPipelineImpl(DeviceImpl* device);

    virtual Result ensureAPIPipelineCreated() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::metal
