#pragma once

#include "cpu-base.h"
#include "cpu-device.h"
#include "cpu-shader-object.h"

namespace rhi::cpu {

class CommandQueueImpl : public CommandQueue
{
public:
    CommandQueueImpl(Device* device, QueueType type);

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

    CommandBufferImpl(Device* device);

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::cpu
