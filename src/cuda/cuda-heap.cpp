#include "cuda-heap.h"
#include "cuda-device.h"
#include "cuda-utils.h"
#include "cuda-command.h"

namespace rhi::cuda {

HeapImpl::HeapImpl(Device* device, const HeapDesc& desc)
    : Heap(device, desc)
{
}

HeapImpl::~HeapImpl() {}

Result HeapImpl::free(HeapAlloc allocation)
{
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    if (deviceImpl->m_queue->m_lastFinishedID == deviceImpl->m_queue->m_lastSubmittedID)
    {
        return retire(allocation);
    }
    else
    {
        PendingFree pendingFree;
        pendingFree.allocation = allocation;
        pendingFree.submitIndex = deviceImpl->m_queue->m_lastSubmittedID;
        m_pendingFrees.push_back(pendingFree);
        return SLANG_OK;
    }
}

Result HeapImpl::flush()
{
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    for (auto it = m_pendingFrees.begin(); it != m_pendingFrees.end();)
    {
        if (it->submitIndex <= deviceImpl->m_queue->m_lastFinishedID)
        {
            SLANG_RETURN_ON_FAIL(retire(it->allocation));
            it = m_pendingFrees.erase(it);
        }
        else
        {
            // List is ordered, so can bail out as soon as we hit
            // a pending free that is not ready yet.
            break;
        }
    }
    return SLANG_OK;
}

Result HeapImpl::allocatePage(const PageDesc& desc, Page** outPage)
{
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    SLANG_CUDA_CTX_SCOPE(deviceImpl);

    CUdeviceptr cudaMemory = 0;
    if (m_desc.memoryType == MemoryType::DeviceLocal)
    {
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAlloc(&cudaMemory, desc.size), deviceImpl);
    }
    else
    {
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAllocHost((void**)&cudaMemory, desc.size), deviceImpl);
    }
    SLANG_RHI_ASSERT((cudaMemory & kAlignment) == 0);

    *outPage = new PageImpl(this, desc, cudaMemory);

    return SLANG_OK;
}

Result HeapImpl::freePage(Page* page_)
{
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    SLANG_CUDA_CTX_SCOPE(deviceImpl);

    PageImpl* page = static_cast<PageImpl*>(page_);
    if (m_desc.memoryType == MemoryType::DeviceLocal)
    {
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemFree(page->m_cudaMemory), deviceImpl);
    }
    else
    {
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemFreeHost((void*)page->m_cudaMemory), deviceImpl);
    }
    delete page;

    return SLANG_OK;
}

Result HeapImpl::fixUpAllocDesc(HeapAllocDesc& desc)
{
    // From scanning CUDA documentation, cuMemAlloc doesn't guarantee more than 128B alignment
    if (desc.alignment > 128)
        return SLANG_E_INVALID_ARG;

    // General pattern of allocating GPU memory is fairly large chunks, so prefer to
    // waste a bit of memory with large alignments than worry about lots of pages
    // with different sizings.
    desc.alignment = 128;
    return SLANG_OK;
}

Result DeviceImpl::createHeap(const HeapDesc& desc, IHeap** outHeap)
{
    SLANG_CUDA_CTX_SCOPE(this);

    RefPtr<HeapImpl> fence = new HeapImpl(this, desc);
    returnComPtr(outHeap, fence);
    return SLANG_OK;
}


} // namespace rhi::cuda
