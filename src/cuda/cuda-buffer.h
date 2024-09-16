#pragma once

#include "cuda-base.h"
#include "cuda-context.h"

namespace rhi::cuda {

class BufferImpl : public Buffer
{
public:
    BufferImpl(RendererBase* device, const BufferDesc& desc)
        : Buffer(device, desc)
    {
    }

    ~BufferImpl();

    uint64_t getBindlessHandle();

    void* m_cudaExternalMemory = nullptr;
    void* m_cudaMemory = nullptr;

    // Hack: This is set when buffer represents a CPU buffer.
    void* m_cpuBuffer = nullptr;

    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL map(MemoryRange* rangeToRead, void** outPointer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unmap(MemoryRange* writtenRange) override;
};

} // namespace rhi::cuda
