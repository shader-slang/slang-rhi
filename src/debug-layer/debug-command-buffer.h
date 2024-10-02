#pragma once

#include "debug-base.h"
#include "debug-command-encoder.h"
#include "debug-shader-object.h"

namespace rhi::debug {

class DebugCommandBuffer : public DebugObject<ICommandBuffer>, ICommandBufferD3D12
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    DebugTransientResourceHeap* m_transientHeap;

private:
    DebugRenderPassEncoder m_renderPassEncoder;
    DebugComputePassEncoder m_computePassEncoder;
    DebugResourcePassEncoder m_resourcePassEncoder;
    DebugRayTracingPassEncoder m_rayTracingPassEncoder;

public:
    DebugCommandBuffer();
    ICommandBuffer* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW Result SLANG_MCALL beginResourcePass(IResourcePassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    beginRenderPass(const RenderPassDesc& desc, IRenderPassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL beginComputePass(IComputePassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL beginRayTracingPass(IRayTracingPassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW void SLANG_MCALL close() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW void SLANG_MCALL invalidateDescriptorHeapBinding() override;
    virtual SLANG_NO_THROW void SLANG_MCALL ensureInternalDescriptorHeapsBound() override;

private:
    void checkEncodersClosedBeforeNewEncoder();
    void checkCommandBufferOpenWhenCreatingEncoder();

public:
    DebugRootShaderObject rootObject;
    bool isOpen = true;
};

} // namespace rhi::debug
