#pragma once

#include "../simple-transient-resource-heap.h"
#include "metal-base.h"
#include "metal-command-encoder.h"
#include "metal-shader-object.h"

namespace rhi::metal {

class CommandBufferImpl : public ICommandBuffer, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandBuffer* getInterface(const Guid& guid);

public:
    RefPtr<DeviceImpl> m_device;
    NS::SharedPtr<MTL::CommandBuffer> m_commandBuffer;
    RootShaderObjectImpl m_rootObject;
    // RefPtr<MutableRootShaderObjectImpl> m_mutableRootShaderObject;

    ResourcePassEncoderImpl m_resourcePassEncoder;
    ComputePassEncoderImpl m_computePassEncoder;
    RenderPassEncoderImpl m_renderPassEncoder;
    RayTracingPassEncoderImpl m_rayTracingPassEncoder;

    NS::SharedPtr<MTL::RenderCommandEncoder> m_metalRenderCommandEncoder;
    NS::SharedPtr<MTL::ComputeCommandEncoder> m_metalComputeCommandEncoder;
    NS::SharedPtr<MTL::AccelerationStructureCommandEncoder> m_metalAccelerationStructureCommandEncoder;
    NS::SharedPtr<MTL::BlitCommandEncoder> m_metalBlitCommandEncoder;

    // Command buffers are deallocated by its command pool,
    // so no need to free individually.
    ~CommandBufferImpl() = default;

    Result init(DeviceImpl* device, TransientResourceHeapImpl* transientHeap);

    void beginCommandBuffer();

    MTL::RenderCommandEncoder* getMetalRenderCommandEncoder(MTL::RenderPassDescriptor* renderPassDesc);
    MTL::ComputeCommandEncoder* getMetalComputeCommandEncoder();
    MTL::AccelerationStructureCommandEncoder* getMetalAccelerationStructureCommandEncoder();
    MTL::BlitCommandEncoder* getMetalBlitCommandEncoder();
    void endMetalCommandEncoder();

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL beginResourcePass(IResourcePassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    beginRenderPass(const RenderPassDesc& desc, IRenderPassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL beginComputePass(IComputePassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL beginRayTracingPass(IRayTracingPassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW void SLANG_MCALL close() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::metal
