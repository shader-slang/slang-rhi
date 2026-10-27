#include "cuda-resource-heap.h"
#include "cuda-device.h"
#include "cuda-utils.h"

namespace rhi::cuda {

ResourceHeapImpl::ResourceHeapImpl(Device* device, const ResourceHeapDesc& desc)
    : ResourceHeap(device, desc)
{
}

ResourceHeapImpl::~ResourceHeapImpl()
{
    CUdeviceptr toFree = m_rawMemory ? m_rawMemory : m_memory;
    if (!toFree)
        return;

    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());
    if (m_isHostMemory)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuMemFreeHost((void*)toFree));
    }
    else
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuMemFree(toFree));
    }
}

Result ResourceHeapImpl::init()
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    m_desc.alignment = max<Size>(m_desc.alignment, 1);
    const Size alignment = m_desc.alignment;
    const Size extra = alignment - 1;
    if (extra != 0 && m_desc.size > ~Size(0) - extra)
        return SLANG_E_INVALID_ARG;
    const Size allocSize = m_desc.size + extra;
    SLANG_CUDA_CTX_SCOPE(device);

    m_isHostMemory = m_desc.memoryType != MemoryType::DeviceLocal;
    if (m_isHostMemory)
    {
        int unifiedAddressing = 0;
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(
            cuDeviceGetAttribute(&unifiedAddressing, CU_DEVICE_ATTRIBUTE_UNIFIED_ADDRESSING, device->m_ctx.device),
            device
        );
        if (!unifiedAddressing)
            return SLANG_E_NOT_AVAILABLE;

        void* hostPtr = nullptr;
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAllocHost(&hostPtr, allocSize), device);
        m_rawMemory = (CUdeviceptr)hostPtr;
    }
    else
    {
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAlloc(&m_rawMemory, allocSize), device);
    }
    m_memory = (m_rawMemory + extra) & ~(CUdeviceptr)extra;
    return SLANG_OK;
}

Result ResourceHeapImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::CUdeviceptr;
    outHandle->value = (uint64_t)m_memory;
    return SLANG_OK;
}

} // namespace rhi::cuda
