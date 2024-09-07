#include "cuda-buffer.h"
#include "cuda-helper-functions.h"

namespace rhi::cuda {

BufferImpl::~BufferImpl()
{
    if (m_cudaMemory)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuMemFree((CUdeviceptr)m_cudaMemory));
    }
}

uint64_t BufferImpl::getBindlessHandle()
{
    return (uint64_t)m_cudaMemory;
}

DeviceAddress BufferImpl::getDeviceAddress()
{
    return (DeviceAddress)m_cudaMemory;
}

Result BufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::CUdeviceptr;
    outHandle->value = getBindlessHandle();
    return SLANG_OK;
}

Result BufferImpl::map(MemoryRange* rangeToRead, void** outPointer)
{
    SLANG_UNUSED(rangeToRead);
    SLANG_UNUSED(outPointer);
    return SLANG_FAIL;
}

Result BufferImpl::unmap(MemoryRange* writtenRange)
{
    SLANG_UNUSED(writtenRange);
    return SLANG_FAIL;
}

} // namespace rhi::cuda
