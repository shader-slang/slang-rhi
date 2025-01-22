#pragma once

#include "cuda-base.h"
#include "cuda-device.h"
#include "cuda-helper-functions.h"
#include "cuda-pipeline.h"
#include "cuda-shader-object.h"

namespace rhi::cuda {

class CommandQueueImpl : public CommandQueue<DeviceImpl>
{
public:
    CUstream m_stream;

    bool m_computeStateValid = false;
    RefPtr<ComputePipelineImpl> m_computePipeline;
    RefPtr<RootShaderObjectImpl> m_rootObject;

    CommandQueueImpl(DeviceImpl* device, QueueType type);
    ~CommandQueueImpl();

    // ICommandQueue implementation

    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandEncoder(ICommandEncoder** outEncoder) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    submit(uint32_t count, ICommandBuffer* const* commandBuffers, IFence* fence, uint64_t valueToSignal) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL waitOnHost() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class CommandEncoderImpl : public CommandEncoder
{
public:
    DeviceImpl* m_device;
    RefPtr<CommandBufferImpl> m_commandBuffer;

    CommandEncoderImpl(DeviceImpl* device);

    Result init();

    // ICommandEncoder implementation

    virtual SLANG_NO_THROW Result SLANG_MCALL finish(ICommandBuffer** outCommandBuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class CommandBufferImpl : public CommandBuffer
{
public:
    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::cuda
