#include "cuda-heap.h"
#include "cuda-device.h"
#include "cuda-utils.h"
#include "cuda-command.h"

namespace rhi::cuda {

// ============================================================================
// PageImpl - Stream tracking implementation (PyTorch-style)
// ============================================================================

HeapImpl::PageImpl::~PageImpl()
{
    // Wait on and clean up any remaining events.
    // We must call cuEventSynchronize before cuEventDestroy because
    // cuEventDestroy does NOT wait for the event to complete - it just
    // marks the event for destruction. Without sync, we could destroy
    // an event while GPU work referencing this page is still pending.
    for (auto& se : m_pendingEvents)
    {
        if (se.event)
        {
            SLANG_CUDA_ASSERT_ON_FAIL(cuEventSynchronize(se.event));
            SLANG_CUDA_ASSERT_ON_FAIL(cuEventDestroy(se.event));
        }
    }
    m_pendingEvents.clear();
}

void HeapImpl::PageImpl::recordStreamUse(void* stream)
{
    CUstream cudaStream = static_cast<CUstream>(stream);

    // Following PyTorch model: only add events when stream differs from allocation stream
    if (cudaStream == m_stream || cudaStream == nullptr)
    {
        return;
    }

    // Check if we already have an event for this stream
    for (const auto& se : m_pendingEvents)
    {
        if (se.stream == cudaStream)
        {
            // Already tracking this stream - update the event by re-recording
            // This ensures we track the latest work on this stream
            SLANG_CUDA_ASSERT_ON_FAIL(cuEventRecord(se.event, cudaStream));
            return;
        }
    }

    // Create new event for this stream
    CUevent event;
    SLANG_CUDA_ASSERT_ON_FAIL(cuEventCreate(&event, CU_EVENT_DISABLE_TIMING));

    // Record the event on the stream
    SLANG_CUDA_ASSERT_ON_FAIL(cuEventRecord(event, cudaStream));

    m_pendingEvents.push_back({cudaStream, event});
}

bool HeapImpl::PageImpl::canReuse() const
{
    // If no pending events, page is safe to reuse
    if (m_pendingEvents.empty())
    {
        return true;
    }

    // Check if all events have completed
    for (const auto& se : m_pendingEvents)
    {
        CUresult result = cuEventQuery(se.event);
        if (result == CUDA_ERROR_NOT_READY)
        {
            // At least one stream is still using this page
            return false;
        }
        // CUDA_SUCCESS means event completed.
        // Assert on unexpected errors (e.g., CUDA_ERROR_INVALID_VALUE if context destroyed).
        // This catches issues in debug builds while preventing deadlocks in release.
        SLANG_RHI_ASSERT(result == CUDA_SUCCESS);
    }

    return true;
}

void HeapImpl::PageImpl::processEvents()
{
    // Remove completed events
    auto it = m_pendingEvents.begin();
    while (it != m_pendingEvents.end())
    {
        CUresult result = cuEventQuery(it->event);
        if (result == CUDA_ERROR_NOT_READY)
        {
            // Event still pending - keep it
            ++it;
        }
        else
        {
            // CUDA_SUCCESS means event completed.
            // Assert on unexpected errors to catch issues in debug builds.
            SLANG_RHI_ASSERT(result == CUDA_SUCCESS);
            // Clean up the event (defensive: proceed even if assert disabled in release)
            SLANG_CUDA_ASSERT_ON_FAIL(cuEventDestroy(it->event));
            it = m_pendingEvents.erase(it);
        }
    }
}

bool HeapImpl::PageImpl::processEventsAndCheckReuse()
{
    // Combined operation: process events and check reusability in a single pass.
    // This is more efficient than processEvents() + canReuse() and avoids the
    // race condition where events complete between the two separate calls.
    auto it = m_pendingEvents.begin();
    while (it != m_pendingEvents.end())
    {
        CUresult result = cuEventQuery(it->event);
        if (result == CUDA_ERROR_NOT_READY)
        {
            // At least one event still pending - page cannot be reused yet
            return false;
        }
        // CUDA_SUCCESS means event completed.
        // Assert on unexpected errors to catch issues in debug builds.
        SLANG_RHI_ASSERT(result == CUDA_SUCCESS);
        // Clean up the completed event
        SLANG_CUDA_ASSERT_ON_FAIL(cuEventDestroy(it->event));
        it = m_pendingEvents.erase(it);
    }
    // All events processed and completed - page is safe to reuse
    return true;
}

void HeapImpl::PageImpl::notifyUse()
{
    // Get the current stream from the heap
    HeapImpl* heap = static_cast<HeapImpl*>(m_heap);
    CUstream currentStream = heap->getCurrentStream();

    // If this page is being used by a different stream than it was allocated on,
    // record the cross-stream usage for proper synchronization.
    // Following PyTorch model: only track when streams actually differ.
    if (currentStream != nullptr && m_stream != currentStream)
    {
        recordStreamUse(currentStream);
    }
}

// ============================================================================
// PageCache - PyTorch-style caching allocator
// ============================================================================

HeapImpl::PageImpl* HeapImpl::PageCache::findReusable(Size size, CUstream stream)
{
    // PyTorch-style optimization: prefer pages allocated on the same stream
    // First pass: look for exact stream match (no synchronization needed)
    for (auto it = m_cachedPages.begin(); it != m_cachedPages.end(); ++it)
    {
        PageImpl* page = *it;

        // Check size match
        if (page->m_desc.size != size)
            continue;

        // Prefer page from same stream (no events to wait on)
        if (page->m_stream == stream)
        {
            // Process events and check reusability in a single pass.
            // This avoids the race condition where events complete between
            // separate processEvents() and canReuse() calls.
            if (!page->processEventsAndCheckReuse())
                continue;

            // Found a reusable page from same stream - best case
            m_cachedPages.erase(it);
            return page;
        }
    }

    // Second pass: accept any reusable page (may need synchronization later)
    for (auto it = m_cachedPages.begin(); it != m_cachedPages.end(); ++it)
    {
        PageImpl* page = *it;

        // Check size match
        if (page->m_desc.size != size)
            continue;

        // Skip same-stream pages (already checked above)
        if (page->m_stream == stream)
            continue;

        // Process events and check reusability in a single pass
        if (!page->processEventsAndCheckReuse())
            continue;

        // Found a reusable page from different stream
        m_cachedPages.erase(it);
        return page;
    }

    return nullptr;
}

void HeapImpl::PageCache::insert(PageImpl* page)
{
    m_cachedPages.push_back(page);
}

void HeapImpl::PageCache::remove(PageImpl* page)
{
    m_cachedPages.remove(page);
}

void HeapImpl::PageCache::releaseAll(DeviceImpl* device, MemoryType memType)
{
    SLANG_CUDA_CTX_SCOPE(device);

    for (PageImpl* page : m_cachedPages)
    {
        // PageImpl destructor will clean up pending events
        if (memType == MemoryType::DeviceLocal)
        {
            SLANG_CUDA_ASSERT_ON_FAIL(cuMemFree(page->m_cudaMemory));
        }
        else
        {
            SLANG_CUDA_ASSERT_ON_FAIL(cuMemFreeHost((void*)page->m_cudaMemory));
        }
        delete page;
    }
    m_cachedPages.clear();
}

Size HeapImpl::PageCache::getCachedSize() const
{
    Size total = 0;
    for (const PageImpl* page : m_cachedPages)
    {
        total += page->m_desc.size;
    }
    return total;
}

// ============================================================================
// HeapImpl
// ============================================================================

HeapImpl::HeapImpl(Device* device, const HeapDesc& desc)
    : Heap(device, desc)
    , m_cachingConfig(desc.caching)
{
}

CUstream HeapImpl::getCurrentStream() const
{
    // Query the device for the current stream.
    // This is set by the command queue on submit, so all heaps
    // automatically see the current stream without needing individual updates.
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(m_device.get());
    return deviceImpl->getCurrentStream();
}

HeapImpl::~HeapImpl()
{
    // Release all cached pages
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    m_pageCache.releaseAll(deviceImpl, m_desc.memoryType);
}

Result HeapImpl::free(HeapAlloc allocation)
{
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());

    // PyTorch-style optimization: Same-stream immediate reuse
    //
    // Key insight: CUDA stream FIFO ordering means operations on the same stream
    // execute in order. When we "free" and "allocate" memory on the same stream,
    // these are CPU-side pool operations - the GPU never sees them.
    //
    // Timeline:
    //   CPU: [Launch K1(A)] → [Free A] → [Alloc A] → [Launch K2(A)]
    //                ▼       (pool op)  (pool op)         ▼
    //   GPU:    [Kernel K1 uses A] ──────────────► [Kernel K2 uses A]
    //                              FIFO guarantees K2 waits for K1
    //
    // This is SAFE and allows immediate memory reuse without waiting!
    PageImpl* page = static_cast<PageImpl*>(allocation.pageId);

    // Check if this is a same-stream allocation with no cross-stream usage
    CUstream currentStream = getCurrentStream();
    bool sameStream = (page->m_stream == currentStream);
    bool noCrossStreamEvents = page->canReuse();  // No pending cross-stream events

    if (sameStream && noCrossStreamEvents)
    {
        // Same stream, no cross-stream usage: IMMEDIATE reuse (PyTorch fast path)
        // This is the key optimization - no waiting for stream idle!
        return retire(allocation);
    }

    // Fallback cases that require deferred retirement:
    // 1. Stream is idle (old conservative path)
    if (deviceImpl->m_queue->m_lastFinishedID == deviceImpl->m_queue->m_lastSubmittedID)
    {
        return retire(allocation);
    }

    // 2. Different stream or has cross-stream events: defer until safe
    PendingFree pendingFree;
    pendingFree.allocation = allocation;
    pendingFree.submitIndex = deviceImpl->m_queue->m_lastSubmittedID;
    m_pendingFrees.push_back(pendingFree);
    return SLANG_OK;
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

    CUstream currentStream = getCurrentStream();

    // Try to find a reusable page in the cache first (PyTorch-style caching)
    if (m_cachingConfig.enabled)
    {
        PageImpl* cachedPage = m_pageCache.findReusable(desc.size, currentStream);
        if (cachedPage)
        {
            // Reusing cached page - it keeps its original allocation stream (m_stream)
            // This follows PyTorch model where ownership never transfers
            *outPage = cachedPage;
            return SLANG_OK;
        }
    }

    // No cached page available - allocate new one
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

    PageImpl* newPage = new PageImpl(this, desc, cudaMemory);

    // Set the allocation stream for the new page (PyTorch model: set once, never changes)
    newPage->m_stream = currentStream;

    *outPage = newPage;
    return SLANG_OK;
}

Result HeapImpl::freePage(Page* page_)
{
    PageImpl* page = static_cast<PageImpl*>(page_);

    // PyTorch-style caching: don't actually free, cache for reuse
    if (m_cachingConfig.enabled)
    {
        // Process any completed events before caching
        page->processEvents();

        // Cache the page for later reuse
        m_pageCache.insert(page);
        return SLANG_OK;
    }

    // Caching disabled - actually free the memory
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    SLANG_CUDA_CTX_SCOPE(deviceImpl);

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
