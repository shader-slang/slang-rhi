#pragma once

#include "vk-base.h"
#include "vk-device.h"
#include "../buffer-pool.h"

#include <vector>
#include <list>

namespace rhi::vk {

class CommandQueueImpl : public CommandQueue<DeviceImpl>
{
public:
    VulkanApi& m_api;
    VkQueue m_queue;
    uint32_t m_queueFamilyIndex;
    struct FenceWaitInfo
    {
        RefPtr<FenceImpl> fence;
        uint64_t waitValue;
    };
    std::vector<FenceWaitInfo> m_pendingWaitFences;
    VkSemaphore m_pendingWaitSemaphores[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};

    VkSemaphore m_semaphore;
    VkSemaphore m_trackingSemaphore;
    uint64_t m_lastSubmittedID = 0;
    uint64_t m_lastFinishedID = 0;

    std::mutex m_mutex;
    std::list<RefPtr<CommandBufferImpl>> m_commandBuffersPool;
    std::list<RefPtr<CommandBufferImpl>> m_commandBuffersInFlight;

    CommandQueueImpl(DeviceImpl* device, QueueType type);
    ~CommandQueueImpl();

    void init(VkQueue queue, uint32_t queueFamilyIndex);

    Result createCommandBuffer(CommandBufferImpl** outCommandBuffer);
    Result getOrCreateCommandBuffer(CommandBufferImpl** outCommandBuffer);
    void retireUnfinishedCommandBuffer(CommandBufferImpl* commandBuffer);
    void retireCommandBuffers();
    uint64_t updateLastFinishedID();

    // ICommandQueue implementation

    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandEncoder(ICommandEncoder** outEncoder) override;

    virtual SLANG_NO_THROW void SLANG_MCALL waitOnHost() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues) override;

    void queueSubmitImpl(uint32_t count, ICommandBuffer* const* commandBuffers, IFence* fence, uint64_t valueToSignal);

    virtual SLANG_NO_THROW Result SLANG_MCALL
    submit(GfxCount count, ICommandBuffer* const* commandBuffers, IFence* fence, uint64_t valueToSignal) override;
};

class CommandEncoderImpl : public CommandEncoder
{
public:
    DeviceImpl* m_device;
    CommandQueueImpl* m_queue;
    RefPtr<CommandBufferImpl> m_commandBuffer;

    CommandEncoderImpl(DeviceImpl* device, CommandQueueImpl* queue);
    ~CommandEncoderImpl();

    Result init();

    // ICommandEncoder implementation

    virtual SLANG_NO_THROW void SLANG_MCALL uploadTextureData(
        ITexture* dst,
        SubresourceRange subresourceRange,
        Offset3D offset,
        Extents extent,
        SubresourceData* subresourceData,
        GfxCount subresourceDataCount
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL finish(ICommandBuffer** outCommandBuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class CommandBufferImpl : public CommandBuffer
{
public:
    DeviceImpl* m_device;
    CommandQueueImpl* m_queue;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    DescriptorSetAllocator m_descriptorSetAllocator;
    BufferPool<DeviceImpl, BufferImpl> m_constantBufferPool;
    BufferPool<DeviceImpl, BufferImpl> m_uploadBufferPool;
    uint64_t m_submissionID = 0;

    CommandBufferImpl(DeviceImpl* device, CommandQueueImpl* queue);
    ~CommandBufferImpl();

    Result init();
    Result reset();

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::vk
