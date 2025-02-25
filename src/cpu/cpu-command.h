#pragma once

#include "cpu-base.h"
#include "cpu-device.h"
#include "cpu-shader-object.h"

namespace rhi::cpu {

class CommandQueueImpl : public CommandQueue<DeviceImpl>
{
public:
    CommandQueueImpl(DeviceImpl* device, QueueType type);

    // ICommandQueue implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandEncoder(ICommandEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL submit(const SubmitDesc& desc) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL waitOnHost() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class CommandEncoderImpl : public CommandEncoder
{
public:
    DeviceImpl* m_device;
    RefPtr<CommandBufferImpl> m_commandBuffer;

    CommandEncoderImpl(DeviceImpl* device);

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
    BindingCache m_bindingCache;

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::cpu
