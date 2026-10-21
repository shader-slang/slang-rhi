#pragma once

#include "wgpu-base.h"
#include "wgpu-shader-object.h"
#include "wgpu-constant-buffer-pool.h"

#include <set>

namespace rhi::wgpu {

class CommandQueueImpl : public CommandQueue
{
public:
    WGPUQueue m_queue = nullptr;

    CommandQueueImpl(Device* device, QueueType type);
    ~CommandQueueImpl();

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
    // Destinations are retained by m_commandList. Mark their CPU copies stale only on submission.
    std::set<BufferImpl*> m_uploadBufferWriteTargets;

    CommandBufferImpl(Device* device, CommandQueueImpl* queue);
    ~CommandBufferImpl();

    virtual Result reset() override;

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::wgpu
