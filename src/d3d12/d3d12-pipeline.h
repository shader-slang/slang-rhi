#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class RenderPipelineImpl : public RenderPipelineBase
{
public:
    RenderPipelineImpl(DeviceImpl* device);
    Result init(const RenderPipelineDesc& inDesc);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    RefPtr<DeviceImpl> m_device;
    ComPtr<ID3D12PipelineState> m_pipelineState;
};

class ComputePipelineImpl : public ComputePipelineBase
{
public:
    ComputePipelineImpl(DeviceImpl* device);
    Result init(const ComputePipelineDesc& desc);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    RefPtr<DeviceImpl> m_device;
    ComPtr<ID3D12PipelineState> m_pipelineState;
};

#if SLANG_RHI_DXR
class RayTracingPipelineImpl : public RayTracingPipelineBase
{
public:
    RayTracingPipelineImpl(DeviceImpl* device);
    Result init(const RayTracingPipelineDesc& inDesc);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    RefPtr<DeviceImpl> m_device;
    ComPtr<ID3D12StateObject> m_stateObject;
};
#endif

} // namespace rhi::d3d12
