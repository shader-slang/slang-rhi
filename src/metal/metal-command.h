#pragma once

#include "metal-base.h"
#include "metal-shader-object.h"

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

    // ICommandQueue implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandEncoder(ICommandEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL waitOnHost() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFenceValuesOnDevice(uint32_t fenceCount, IFence** fences, uint64_t* waitValues) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    submit(uint32_t count, ICommandBuffer** commandBuffers, IFence* fence, uint64_t valueToSignal) override;
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

    virtual Device* getDevice() override;
    virtual Result getBindingData(RootShaderObject* rootObject, BindingData*& outBindingData) override;

    // ICommandEncoder implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL finish(ICommandBuffer** outCommandBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class CommandBufferImpl : public CommandBuffer
{
public:
    DeviceImpl* m_device;
    CommandQueueImpl* m_queue;
    NS::SharedPtr<MTL::CommandBuffer> m_commandBuffer;
    BindingCache m_bindingCache;

    CommandBufferImpl(DeviceImpl* device, CommandQueueImpl* queue);
    ~CommandBufferImpl();

    Result init();
    virtual Result reset() override;

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::metal
