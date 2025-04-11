#pragma once

#include "cuda-base.h"
#include "cuda-constant-buffer-pool.h"
#include "cuda-shader-object.h"

namespace rhi::cuda {

class CommandQueueImpl : public CommandQueue
{
public:
    CUstream m_stream;

    CommandQueueImpl(Device* device, QueueType type);
    ~CommandQueueImpl();

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

    CommandBufferImpl(Device* device);
    virtual ~CommandBufferImpl() = default;

    virtual Result reset() override;

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::cuda
