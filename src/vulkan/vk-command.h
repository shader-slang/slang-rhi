#pragma once

#include "vk-base.h"
#include "vk-shader-object.h"
#include "vk-constant-buffer-pool.h"

#include <vector>
#include <list>

namespace rhi::vk {

class CommandQueueImpl : public CommandQueue
{
public:
    VulkanApi& m_api;
    VkQueue m_queue;
    uint32_t m_queueFamilyIndex;

    // Set by the surface for synchronization.
    struct
    {
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    } m_surfaceSync;

    VkSemaphore m_trackingSemaphore;
    uint64_t m_lastSubmittedID = 0;
    uint64_t m_lastFinishedID = 0;

    std::mutex m_mutex;
    std::list<RefPtr<CommandBufferImpl>> m_commandBuffersPool;
    std::list<RefPtr<CommandBufferImpl>> m_commandBuffersInFlight;

    CommandQueueImpl(Device* device, QueueType type);
    ~CommandQueueImpl();

    void init(VkQueue queue, uint32_t queueFamilyIndex);

    Result createCommandBuffer(CommandBufferImpl** outCommandBuffer);
    Result getOrCreateCommandBuffer(CommandBufferImpl** outCommandBuffer);
    void retireCommandBuffer(CommandBufferImpl* commandBuffer);
    void retireCommandBuffers();
    uint64_t updateLastFinishedID();

    // ICommandQueue implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandEncoder(ICommandEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL submit(const SubmitDesc& desc) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL waitOnHost() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class CommandEncoderImpl : public CommandEncoder
{
public:
    CommandQueueImpl* m_queue;
    RefPtr<CommandBufferImpl> m_commandBuffer;

    CommandEncoderImpl(Device* device, CommandQueueImpl* queue);
    ~CommandEncoderImpl();

    Result init();

    virtual Result getBindingData(RootShaderObject* rootObject, BindingData*& outBindingData) override;

    // ICommandEncoder implementation

    virtual SLANG_NO_THROW Result SLANG_MCALL finish(ICommandBuffer** outCommandBuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class CommandBufferImpl : public CommandBuffer
{
public:
    CommandQueueImpl* m_queue;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    ConstantBufferPool m_constantBufferPool;
    DescriptorSetAllocator m_descriptorSetAllocator;
    BindingCache m_bindingCache;
    uint64_t m_submissionID = 0;

    CommandBufferImpl(Device* device, CommandQueueImpl* queue);
    ~CommandBufferImpl();

    Result init();
    virtual Result reset() override;

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::vk
