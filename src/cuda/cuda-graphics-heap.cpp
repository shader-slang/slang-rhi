#include "cuda-graphics-heap.h"
#include "cuda-device.h"
#include "cuda-utils.h"
#include "cuda-command.h"

namespace rhi::cuda {

GraphicsHeapImpl::GraphicsHeapImpl(Device* device, const GraphicsHeapDesc& desc)
    : GraphicsHeap(device, desc)
{
}

GraphicsHeapImpl::~GraphicsHeapImpl() {}

Result GraphicsHeapImpl::free(GraphicsAllocation allocation)
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

Result GraphicsHeapImpl::checkPendingFrees()
{
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    for (auto it = m_pendingFrees.begin(); it != m_pendingFrees.end();)
    {
        if (it->submitIndex <= deviceImpl->m_queue->m_submitCompleted)
        {
            retire(it->allocation);
            it = m_pendingFrees.erase(it);
        }
        else
        {
            // List is ordered, so can bail out as soon as we hit
            // a pending free that is not ready yet.
            break;
        }
    }
}

Result GraphicsHeapImpl::allocatePage(const PageDesc& desc, Page** page)
{

    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    SLANG_CUDA_CTX_SCOPE(deviceImpl);

    CUdeviceptr cudaMemory = 0;
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAlloc(&cudaMemory, desc.size), deviceImpl);

    *page = new PageImpl(this, desc, cudaMemory);

    return SLANG_OK;
}

Result GraphicsHeapImpl::freePage(Page* page_)
{
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    SLANG_CUDA_CTX_SCOPE(deviceImpl);

    PageImpl* page = static_cast<PageImpl*>(page_);
    cuMemFree(page->m_cudaMemory);
    delete page;

    return SLANG_OK;
}

Result DeviceImpl::createGraphicsHeap(const GraphicsHeapDesc& desc, IGraphicsHeap** outHeap)
{
    SLANG_CUDA_CTX_SCOPE(this);

    RefPtr<GraphicsHeapImpl> fence = new GraphicsHeapImpl(this, desc);
    returnComPtr(outHeap, fence);
    return SLANG_OK;
}

} // namespace rhi::cuda
