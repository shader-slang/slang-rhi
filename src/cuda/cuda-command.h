#pragma once

#include "cuda-base.h"
#include "cuda-constant-buffer-pool.h"
#include "cuda-shader-object.h"

namespace rhi::cuda {

class CommandQueueImpl : public CommandQueue
{
public:
    // Helper to switch to a stream and be sure we switch back on
    // leaving scope, even if we had to bail out due to errors.
    struct StreamScope
    {
        CommandQueueImpl* m_queue;

        StreamScope(CommandQueueImpl* queue, CUstream stream)
            : m_queue(queue)
        {
            m_queue->m_activeStream = stream;
        }

        ~StreamScope() { m_queue->m_activeStream = (CUstream)kInvalidCUDAStream; }
    };

    CUstream m_defaultStream;
    CUstream m_activeStream;
    std::list<RefPtr<CommandBufferImpl>> m_commandBuffersInFlight;

    CommandQueueImpl(Device* device, QueueType type);
    ~CommandQueueImpl();
    Result retireCommandBuffers();

    // ICommandQueue implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandEncoder(ICommandEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL submit(const SubmitDesc& desc) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL waitOnHost() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class CommandEncoderImpl : public CommandEncoder
{
public:
    RefPtr<CommandBufferImpl> m_commandBuffer;

    CommandEncoderImpl(Device* device);

    Result init();

    virtual Result getBindingData(RootShaderObject* rootObject, BindingData*& outBindingData) override;

    // ICommandEncoder implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL finish(ICommandBuffer** outCommandBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class CommandBufferImpl : public CommandBuffer
{
public:
    BindingCache m_bindingCache;
    ConstantBufferPool m_constantBufferPool;
    CUevent m_completionEvent = nullptr;

    CommandBufferImpl(Device* device);
    virtual ~CommandBufferImpl() = default;

    virtual Result reset() override;

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::cuda
