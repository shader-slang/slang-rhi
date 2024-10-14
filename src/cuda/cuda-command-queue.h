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
    CUstream stream;

    bool m_computeStateValid = false;
    RefPtr<ComputePipelineImpl> m_computePipeline;
    RefPtr<RootShaderObjectImpl> m_rootObject;

    CommandQueueImpl(DeviceImpl* device, QueueType type);
    ~CommandQueueImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandEncoder(ICommandEncoder** outEncoder) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    submit(GfxCount count, ICommandBuffer* const* commandBuffers, IFence* fence, uint64_t valueToSignal) override;

    virtual SLANG_NO_THROW void SLANG_MCALL waitOnHost() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    void setComputeState(const ComputeState& state);

    void dispatchCompute(int x, int y, int z);

    void copyBuffer(IBuffer* dst, size_t dstOffset, IBuffer* src, size_t srcOffset, size_t size);

    void uploadBufferData(IBuffer* dst, size_t offset, size_t size, void* data);

    void writeTimestamp(IQueryPool* pool, SlangInt index);

    void execute(CommandBufferImpl* commandBuffer);
};

} // namespace rhi::cuda
