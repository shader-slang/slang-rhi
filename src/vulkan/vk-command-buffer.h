#pragma once

#include "vk-base.h"
#include "vk-command-encoder.h"
#include "vk-shader-object.h"
#include "vk-transient-heap.h"

#include "../state-tracking.h"

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
    DeviceImpl* m_device;
    BreakableReference<TransientResourceHeapImpl> m_transientHeap;
    bool m_isPreCommandBufferEmpty = true;
    RootShaderObjectImpl m_rootObject;
    RefPtr<MutableRootShaderObjectImpl> m_mutableRootShaderObject;

    StateTracking m_stateTracking;

    ResourcePassEncoderImpl m_resourcePassEncoder;
    RenderPassEncoderImpl m_renderPassEncoder;
    ComputePassEncoderImpl m_computePassEncoder;
    RayTracingPassEncoderImpl m_rayTracingPassEncoder;

    // Command buffers are deallocated by its command pool,
    // so no need to free individually.
    ~CommandBufferImpl() = default;

    Result init(DeviceImpl* device, VkCommandPool pool, TransientResourceHeapImpl* transientHeap);

    void beginCommandBuffer();

    Result createPreCommandBuffer();

    VkCommandBuffer getPreCommandBuffer();

    void requireBufferState(BufferImpl* buffer, ResourceState state);
    void requireTextureState(TextureImpl* texture, SubresourceRange subresourceRange, ResourceState state);
    void commitBarriers();

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL beginResourcePass(IResourcePassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    beginRenderPass(const RenderPassDesc& desc, IRenderPassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL beginComputePass(IComputePassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL beginRayTracingPass(IRayTracingPassEncoder** outEncoder) override;
    virtual SLANG_NO_THROW void SLANG_MCALL close() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::vk
