#pragma once

#include "cuda-base.h"
#include "cuda-constant-buffer-pool.h"
#include "cuda-shader-object.h"

namespace rhi::cuda {

struct SubmitEvent
{
    CUevent event = nullptr;
    uint64_t submitID = 0;
};

class CommandQueueImpl : public CommandQueue
{
public:
    CUstream m_stream;

    uint64_t m_lastSubmittedID = 0;
    uint64_t m_lastFinishedID = 0;

    std::list<RefPtr<CommandBufferImpl>> m_commandBuffersInFlight;
    std::list<SubmitEvent> m_submitEvents;

    CommandQueueImpl(Device* device, QueueType type);
    ~CommandQueueImpl();
    Result retireCommandBuffers();

    Result signalFence(CUstream stream, uint64_t* outId);
    Result updateFence();

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
    uint64_t m_submissionID = 0;

    CommandBufferImpl(Device* device);
    virtual ~CommandBufferImpl() = default;

    virtual Result reset() override;

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::cuda
