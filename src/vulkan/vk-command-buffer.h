#pragma once

#include "vk-base.h"
#include "vk-command-encoder.h"
#include "vk-shader-object.h"
#include "vk-transient-heap.h"

namespace rhi::vk {

class CommandBufferImpl : public ICommandBuffer, public ComObject
{
public:
    // There are a pair of cyclic references between a `TransientResourceHeap` and
    // a `CommandBuffer` created from the heap. We need to break the cycle when
    // the public reference count of a command buffer drops to 0.
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandBuffer* getInterface(const Guid& guid);
    virtual void comFree() override;

public:
    VkCommandBuffer m_commandBuffer;
    VkCommandBuffer m_preCommandBuffer = VK_NULL_HANDLE;
    VkCommandPool m_pool;
    DeviceImpl* m_renderer;
    BreakableReference<TransientResourceHeapImpl> m_transientHeap;
    bool m_isPreCommandBufferEmpty = true;
    RootShaderObjectImpl m_rootObject;
    RefPtr<MutableRootShaderObjectImpl> m_mutableRootShaderObject;

    ResourceCommandEncoderImpl m_resourceCommandEncoder;
    RenderCommandEncoderImpl m_renderCommandEncoder;
    ComputeCommandEncoderImpl m_computeCommandEncoder;
    RayTracingCommandEncoderImpl m_rayTracingCommandEncoder;

    // Command buffers are deallocated by its command pool,
    // so no need to free individually.
    ~CommandBufferImpl() = default;

    Result init(DeviceImpl* device, VkCommandPool pool, TransientResourceHeapImpl* transientHeap);

    void beginCommandBuffer();

    Result createPreCommandBuffer();

    VkCommandBuffer getPreCommandBuffer();

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL encodeResourceCommands(IResourceCommandEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    encodeRenderCommands(const RenderPassDesc& desc, IRenderCommandEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL encodeComputeCommands(IComputeCommandEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL encodeRayTracingCommands(IRayTracingCommandEncoder** outEncoder) override;
    virtual SLANG_NO_THROW void SLANG_MCALL close() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::vk
