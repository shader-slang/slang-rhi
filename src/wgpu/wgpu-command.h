#pragma once

#include "wgpu-base.h"
#include "wgpu-shader-object.h"
#include "wgpu-constant-buffer-pool.h"

namespace rhi::wgpu {

class CommandQueueImpl : public CommandQueue
{
public:
    WGPUQueue m_queue = nullptr;

    CommandQueueImpl(Device* device, QueueType type);
    ~CommandQueueImpl();

    // ICommandQueue implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandEncoder(ICommandEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL submit(const SubmitDesc& desc) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL waitOnHost() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    uint64_t m_lastSubmittedID = 0;

    std::mutex m_mutex;
    std::list<RefPtr<CommandBufferImpl>> m_commandBuffersPool;
    std::list<RefPtr<CommandBufferImpl>> m_commandBuffersInFlight;

    void shutdown();

    Result createCommandBuffer(CommandBufferImpl** outCommandBuffer);
    Result getOrCreateCommandBuffer(CommandBufferImpl** outCommandBuffer);
    void retireCommandBuffer(CommandBufferImpl* commandBuffer);
    void retireCommandBuffers();
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
    WGPUCommandBuffer m_commandBuffer = nullptr;
    ConstantBufferPool m_constantBufferPool;
    BindingCache m_bindingCache;
    uint64_t m_submissionID = 0;

    CommandBufferImpl(Device* device, CommandQueueImpl* queue);
    ~CommandBufferImpl();

    Result init();
    virtual Result reset() override;

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::wgpu
