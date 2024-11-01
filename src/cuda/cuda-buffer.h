#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

class BufferImpl : public Buffer
{
public:
    BufferImpl(const BufferDesc& desc)
        : Buffer(desc)
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
};

} // namespace rhi::cuda
