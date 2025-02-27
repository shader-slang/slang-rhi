#include "cuda-buffer.h"
#include "cuda-device.h"
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

Result DeviceImpl::createBuffer(const BufferDesc& descIn, const void* initData, IBuffer** outBuffer)
{
    auto desc = fixupBufferDesc(descIn);
    RefPtr<BufferImpl> buffer = new BufferImpl(desc);
    SLANG_CUDA_RETURN_ON_FAIL(cuMemAllocManaged((CUdeviceptr*)(&buffer->m_cudaMemory), desc.size, CU_MEM_ATTACH_GLOBAL)
    );
    if (initData)
    {
        SLANG_CUDA_RETURN_ON_FAIL(cuMemcpy((CUdeviceptr)buffer->m_cudaMemory, (CUdeviceptr)initData, desc.size));
    }
    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::createBufferFromSharedHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer)
{
    if (!handle)
    {
        *outBuffer = nullptr;
        return SLANG_OK;
    }

    RefPtr<BufferImpl> buffer = new BufferImpl(desc);

    // CUDA manages sharing of buffers through the idea of an
    // "external memory" object, which represents the relationship
    // with another API's objects. In order to create this external
    // memory association, we first need to fill in a descriptor struct.
    CUDA_EXTERNAL_MEMORY_HANDLE_DESC externalMemoryHandleDesc;
    memset(&externalMemoryHandleDesc, 0, sizeof(externalMemoryHandleDesc));
    switch (handle.type)
    {
    case NativeHandleType::D3D12Resource:
        externalMemoryHandleDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
        break;
    case NativeHandleType::Win32:
        externalMemoryHandleDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32;
        break;
    default:
        return SLANG_FAIL;
    }
    externalMemoryHandleDesc.handle.win32.handle = (void*)handle.value;
    externalMemoryHandleDesc.size = desc.size;
    externalMemoryHandleDesc.flags = CUDA_EXTERNAL_MEMORY_DEDICATED;

    // Once we have filled in the descriptor, we can request
    // that CUDA create the required association between the
    // external buffer and its own memory.
    CUexternalMemory externalMemory;
    SLANG_CUDA_RETURN_ON_FAIL(cuImportExternalMemory(&externalMemory, &externalMemoryHandleDesc));
    buffer->m_cudaExternalMemory = externalMemory;

    // The CUDA "external memory" handle is not itself a device
    // pointer, so we need to query for a suitable device address
    // for the buffer with another call.
    //
    // Just as for the external memory, we fill in a descriptor
    // structure (although in this case we only need to specify
    // the size).
    CUDA_EXTERNAL_MEMORY_BUFFER_DESC bufferDesc;
    memset(&bufferDesc, 0, sizeof(bufferDesc));
    bufferDesc.size = desc.size;

    // Finally, we can "map" the buffer to get a device address.
    void* deviceAddress;
    SLANG_CUDA_RETURN_ON_FAIL(cuExternalMemoryGetMappedBuffer((CUdeviceptr*)&deviceAddress, externalMemory, &bufferDesc)
    );
    buffer->m_cudaMemory = deviceAddress;

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData)
{
    SLANG_UNUSED(buffer);
    *outData = nullptr;
    return SLANG_E_NOT_AVAILABLE;
}

Result DeviceImpl::unmapBuffer(IBuffer* buffer)
{
    SLANG_UNUSED(buffer);
    return SLANG_E_NOT_AVAILABLE;
}

} // namespace rhi::cuda
