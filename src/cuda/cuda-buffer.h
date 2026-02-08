#pragma once

#include "cuda-base.h"
#include "cuda-heap.h"

namespace rhi::cuda {

class BufferImpl : public Buffer
{
public:
    BufferImpl(Device* device, const BufferDesc& desc);
    ~BufferImpl();

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // IBuffer implementation
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(
        DescriptorHandleAccess access,
        Format format,
        BufferRange range,
        DescriptorHandle* outHandle
    ) override;

public:
    void* m_cudaExternalMemory = nullptr;
    void* m_cudaMemory = nullptr;
    HeapAlloc m_alloc;
};

} // namespace rhi::cuda
