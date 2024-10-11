#pragma once

#include "cuda-base.h"
#include "cuda-command-encoder.h"

namespace rhi::cuda {

class CommandBufferImpl : public ICommandBuffer, public CommandWriter, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandBuffer* getInterface(const Guid& guid);

public:
    DeviceImpl* m_device;
    TransientResourceHeap* m_transientHeap;
    ResourcePassEncoderImpl m_resourcePassEncoder;
    ComputePassEncoderImpl m_computePassEncoder;
#if SLANG_RHI_ENABLE_OPTIX
    RayTracingPassEncoderImpl m_rayTracingPassEncoder;
#endif

    void init(DeviceImpl* device, TransientResourceHeap* transientHeap);

    virtual SLANG_NO_THROW Result SLANG_MCALL beginResourcePass(IResourcePassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    beginRenderPass(const RenderPassDesc& desc, IRenderPassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL beginComputePass(IComputePassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL beginRayTracingPass(IRayTracingPassEncoder** outEncoder) override;

    virtual SLANG_NO_THROW void SLANG_MCALL close() override {}

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::cuda
