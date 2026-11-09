#pragma once

#include "wgpu-base.h"
#include "wgpu-shader-object.h"
#include "wgpu-constant-buffer-pool.h"

#include <list>
#include <mutex>
#include <vector>

namespace rhi::wgpu {

class CommandQueueImpl : public CommandQueue
{
public:
    WGPUQueue m_queue = nullptr;

    struct PendingSubmission
    {
        WGPUFuture future = {};
        WGPUQueueWorkDoneStatus status = WGPUQueueWorkDoneStatus(0);
        bool callbackReceived = false;
        std::vector<InternalRefPtr<CommandBufferImpl>> commandBuffers;
    };

    std::mutex m_mutex;
    std::list<InternalRefPtr<CommandBufferImpl>> m_commandBuffersPool;
    // List nodes keep the callback userdata address stable until WebGPU completes the submission.
    std::list<PendingSubmission> m_submissionsInFlight;

    CommandQueueImpl(Device* device, QueueType type);
    ~CommandQueueImpl();

    Result createCommandBuffer(CommandBufferImpl** outCommandBuffer);
    Result getOrCreateCommandBuffer(CommandBufferImpl** outCommandBuffer);
    void retireCommandBuffer(CommandBufferImpl* commandBuffer);
    Result retireCommandBuffers(bool waitForAll);

    // ICommandQueue implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandEncoder(
        const CommandEncoderDesc& desc,
        ICommandEncoder** outEncoder
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL submit(const SubmitDesc& desc) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL waitOnHost() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class CommandEncoderImpl : public CommandEncoder
{
public:
    CommandQueueImpl* m_queue;
    RefPtr<CommandBufferImpl> m_commandBuffer;

    CommandEncoderImpl(Device* device, CommandQueueImpl* queue, const CommandEncoderDesc& desc);
    ~CommandEncoderImpl();

    Result init();

    virtual Result getBindingData(RootShaderObject* rootObject, BindingData*& outBindingData) override;

    // ICommandEncoder implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL finish(
        const CommandBufferDesc& desc,
        ICommandBuffer** outCommandBuffer
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class CommandBufferImpl : public CommandBuffer
{
public:
    CommandQueueImpl* m_queue;
    WGPUCommandBuffer m_commandBuffer = nullptr;
    ConstantBufferPool m_constantBufferPool;
    BindingCache m_bindingCache;

    CommandBufferImpl(Device* device, CommandQueueImpl* queue);
    ~CommandBufferImpl();

    virtual Result reset() override;

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::wgpu
