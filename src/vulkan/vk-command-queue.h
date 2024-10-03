#pragma once

#include "vk-base.h"
#include "vk-device.h"

#include <vector>

namespace rhi::vk {

class CommandQueueImpl : public CommandQueue<DeviceImpl>
{
public:
    VkQueue m_queue;
    uint32_t m_queueFamilyIndex;
    struct FenceWaitInfo
    {
        RefPtr<FenceImpl> fence;
        uint64_t waitValue;
    };
    std::vector<FenceWaitInfo> m_pendingWaitFences;
    VkSemaphore m_pendingWaitSemaphores[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> m_submitCommandBuffers;
    VkSemaphore m_semaphore;

    CommandQueueImpl(DeviceImpl* device, QueueType type);
    ~CommandQueueImpl();

    void init(VkQueue queue, uint32_t queueFamilyIndex);

    virtual SLANG_NO_THROW void SLANG_MCALL waitOnHost() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues) override;

    void queueSubmitImpl(uint32_t count, ICommandBuffer* const* commandBuffers, IFence* fence, uint64_t valueToSignal);

    virtual SLANG_NO_THROW void SLANG_MCALL
    executeCommandBuffers(GfxCount count, ICommandBuffer* const* commandBuffers, IFence* fence, uint64_t valueToSignal)
        override;
};

} // namespace rhi::vk
