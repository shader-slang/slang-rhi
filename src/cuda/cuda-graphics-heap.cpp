#include "cuda-graphics-heap.h"
#include "cuda-device.h"
#include "cuda-utils.h"
#include "cuda-command.h"

namespace rhi::cuda {

HeapImpl::HeapImpl(Device* device, const GraphicsHeapDesc& desc)
    : Heap(device, desc)
{
}

HeapImpl::~HeapImpl() {}

Result HeapImpl::free(GraphicsAllocation allocation)
{
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    if (deviceImpl->m_queue->m_submitCount == deviceImpl->m_queue->m_submitCompleted)
    {
        return retire(allocation);
    }
    else
    {
        PendingFree pendingFree;
        pendingFree.allocation = allocation;
        pendingFree.submitIndex = deviceImpl->m_queue->m_submitCount;
        m_pendingFrees.push_back(pendingFree);
        return SLANG_OK;
    }
}

Result HeapImpl::flush()
{
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    for (auto it = m_pendingFrees.begin(); it != m_pendingFrees.end();)
    {
        if (it->submitIndex <= deviceImpl->m_queue->m_submitCompleted)
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
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAlloc(&cudaMemory, desc.size), deviceImpl);

    *outPage = new PageImpl(this, desc, cudaMemory);

    return SLANG_OK;
}

Result HeapImpl::freePage(Page* page_)
{
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    SLANG_CUDA_CTX_SCOPE(deviceImpl);

    PageImpl* page = static_cast<PageImpl*>(page_);
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemFree(page->m_cudaMemory), deviceImpl);
    delete page;

    return SLANG_OK;
}

Result DeviceImpl::createGraphicsHeap(const GraphicsHeapDesc& desc, IHeap** outHeap)
{
    SLANG_CUDA_CTX_SCOPE(this);

    RefPtr<HeapImpl> fence = new HeapImpl(this, desc);
    returnComPtr(outHeap, fence);
    return SLANG_OK;
}

} // namespace rhi::cuda
