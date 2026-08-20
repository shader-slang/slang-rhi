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
    if (!m_memory)
        return;

    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());
    if (m_isHostMemory)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuMemFreeHost((void*)m_memory));
    }
    else
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuMemFree(m_memory));
    }
}

Result ResourceHeapImpl::init()
{
    DeviceImpl* device = getDevice<DeviceImpl>();
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
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAllocHost(&hostPtr, m_desc.size), device);
        m_memory = (CUdeviceptr)hostPtr;
    }
    else
    {
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAlloc(&m_memory, m_desc.size), device);
    }
    return SLANG_OK;
}

Result ResourceHeapImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::CUdeviceptr;
    outHandle->value = (uint64_t)m_memory;
    return SLANG_OK;
}

} // namespace rhi::cuda
