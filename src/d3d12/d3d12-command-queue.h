#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class CommandQueueImpl : public CommandQueue<DeviceImpl>
{

public:
    ComPtr<ID3D12Device> m_d3dDevice;
    ComPtr<ID3D12CommandQueue> m_d3dQueue;
    ComPtr<ID3D12Fence> m_fence;
    uint64_t m_fenceValue = 0;
    HANDLE globalWaitHandle;
    uint32_t m_queueIndex = 0;

    CommandQueueImpl(DeviceImpl* device, QueueType type);
    ~CommandQueueImpl();

    Result init(uint32_t queueIndex);

    virtual SLANG_NO_THROW void SLANG_MCALL
    executeCommandBuffers(GfxCount count, ICommandBuffer* const* commandBuffers, IFence* fence, uint64_t valueToSignal)
        override;

    virtual SLANG_NO_THROW void SLANG_MCALL waitOnHost() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::d3d12
