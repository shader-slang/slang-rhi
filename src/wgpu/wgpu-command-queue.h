#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class CommandQueueImpl : public CommandQueue<DeviceImpl>
{
public:
    WGPUQueue m_queue = nullptr;

    CommandQueueImpl(DeviceImpl* device, QueueType type);
    ~CommandQueueImpl();

    // ICommandQueue implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW void SLANG_MCALL waitOnHost() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    submit(GfxCount count, ICommandBuffer* const* commandBuffers, IFence* fence, uint64_t valueToSignal) override;
};

} // namespace rhi::wgpu
