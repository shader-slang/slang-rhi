#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class CommandQueueImpl : public CommandQueue<DeviceImpl>
{
public:
    CommandQueueImpl(DeviceImpl* device, QueueType type);

    // ICommandQueue implementation

    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandEncoder(ICommandEncoder** outEncoder) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    submit(uint32_t count, ICommandBuffer* const* commandBuffers, IFence* fenceToSignal, uint64_t newFenceValue)
        override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL waitOnHost() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues) override;
};

class CommandEncoderImpl : public CommandEncoder
{
public:
    DeviceImpl* m_device;
    RefPtr<CommandBufferImpl> m_commandBuffer;

    CommandEncoderImpl(DeviceImpl* device);

    Result init();

    // ICommandEncoder implementation

    virtual SLANG_NO_THROW Result SLANG_MCALL finish(ICommandBuffer** outCommandBuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class CommandBufferImpl : public CommandBuffer
{
public:
    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::d3d11
