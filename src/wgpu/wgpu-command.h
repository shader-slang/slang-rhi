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
    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandEncoder(ICommandEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL waitOnHost() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFenceValuesOnDevice(uint32_t fenceCount, IFence** fences, uint64_t* waitValues) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    submit(uint32_t count, ICommandBuffer* const* commandBuffers, IFence* fence, uint64_t valueToSignal) override;
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

    virtual SLANG_NO_THROW Result SLANG_MCALL finish(ICommandBuffer** outCommandBuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class CommandBufferImpl : public CommandBuffer
{
public:
    DeviceImpl* m_device;
    CommandQueueImpl* m_queue;
    WGPUCommandBuffer m_commandBuffer = nullptr;

    CommandBufferImpl(DeviceImpl* device, CommandQueueImpl* queue);
    ~CommandBufferImpl();

    Result init();
    Result reset();

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::wgpu
