#pragma once

#include "metal-base.h"
#include "metal-device.h"

namespace rhi::metal {

class CommandQueueImpl : public CommandQueue<DeviceImpl>
{
public:
    NS::SharedPtr<MTL::CommandQueue> m_commandQueue;

    struct FenceWaitInfo
    {
        RefPtr<FenceImpl> fence;
        uint64_t waitValue;
    };
    std::vector<FenceWaitInfo> m_pendingWaitFences;

    CommandQueueImpl(DeviceImpl* device, QueueType type);
    ~CommandQueueImpl();

    void init(NS::SharedPtr<MTL::CommandQueue> commandQueue);

    virtual SLANG_NO_THROW void SLANG_MCALL waitOnHost() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues) override;

    void queueSubmitImpl(uint32_t count, ICommandBuffer* const* commandBuffers, IFence* fence, uint64_t valueToSignal);

    virtual SLANG_NO_THROW void SLANG_MCALL
    executeCommandBuffers(GfxCount count, ICommandBuffer* const* commandBuffers, IFence* fence, uint64_t valueToSignal)
        override;
};

} // namespace rhi::metal
