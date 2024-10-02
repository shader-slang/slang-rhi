#pragma once

#include "wgpu-base.h"
#include "wgpu-shader-object.h"
#include "wgpu-command-encoder.h"

namespace rhi::wgpu {

class TransientResourceHeapImpl;

class CommandBufferImpl : public ICommandBuffer, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandBuffer* getInterface(const Guid& guid);

public:
    RefPtr<DeviceImpl> m_device;
    WGPUCommandBuffer m_commandBuffer = nullptr;
    WGPUCommandEncoder m_commandEncoder = nullptr;
    BreakableReference<TransientResourceHeapImpl> m_transientHeap;
    RootShaderObjectImpl m_rootObject;
    RefPtr<MutableRootShaderObjectImpl> m_mutableRootShaderObject;

    ResourcePassEncoderImpl m_resourcePassEncoder;
    RenderPassEncoderImpl m_renderPassEncoder;
    ComputePassEncoderImpl m_computePassEncoder;

    ~CommandBufferImpl();

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL beginResourcePass(IResourcePassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    beginRenderPass(const RenderPassDesc& desc, IRenderPassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL beginComputePass(IComputePassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL beginRayTracingPass(IRayTracingPassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW void SLANG_MCALL close() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::wgpu
