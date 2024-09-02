#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class PipelineImpl : public PipelineBase
{
public:
    PipelineImpl(DeviceImpl* device)
        : m_device(device)
    {
    }
    DeviceImpl* m_device;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    void init(const RenderPipelineDesc& inDesc);
    void init(const ComputePipelineDesc& inDesc);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual Result ensureAPIPipelineCreated() override;
};

#if SLANG_RHI_DXR
class RayTracingPipelineImpl : public PipelineBase
{
public:
    ComPtr<ID3D12StateObject> m_stateObject;
    DeviceImpl* m_device;
    RayTracingPipelineImpl(DeviceImpl* device);
    void init(const RayTracingPipelineDesc& inDesc);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual Result ensureAPIPipelineCreated() override;
};
#endif

} // namespace rhi::d3d12
