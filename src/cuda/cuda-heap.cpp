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
    // Note: nullptr (default stream) is a valid stream that needs tracking if it differs
    // from m_stream. We don't skip nullptr here because cross-stream usage with the
    // default stream still requires synchronization.
    if (cudaStream == m_stream)
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

void HeapImpl::PageImpl::notifyUse(void* stream)
{
    // If no stream context provided (kNoStream sentinel), nothing to track
    if (stream == kNoStream)
    {
        return;
    }

    CUstream currentStream = static_cast<CUstream>(stream);

    // If page has no stream yet (kNoStream), adopt this stream as owner (lazy assignment)
    if (m_stream == kNoStream)
    {
        m_stream = currentStream;
        return;
    }

    // If this page is being used by a different stream than it was allocated on,
    // record the cross-stream usage for proper synchronization.
    // Following PyTorch model: only track when streams actually differ.
    if (m_stream != currentStream)
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

HeapImpl::~HeapImpl()
{
    // Release all cached pages
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    m_pageCache.releaseAll(deviceImpl, m_desc.memoryType);
}

Result HeapImpl::free(HeapAlloc allocation)
{
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    PageImpl* page = static_cast<PageImpl*>(allocation.pageId);

    // PyTorch-style optimization: Immediate reuse when safe
    //
    // Key insight: CUDA stream FIFO ordering means operations on the same stream
    // execute in order. If a page has no cross-stream events, it was only used
    // by its owner stream, so any new allocation will be properly ordered.

    // Case 1: Page has no stream assignment - no GPU work was ever done with it
    if (page->m_stream == kNoStream)
    {
        return retire(allocation);
    }

    // Case 2: Queue is completely idle - all GPU work is done
    if (deviceImpl->m_queue->m_lastFinishedID == deviceImpl->m_queue->m_lastSubmittedID)
    {
        return retire(allocation);
    }

    // Case 3: No cross-stream events - page was only used by its owner stream
    // CUDA stream ordering guarantees safety for immediate reuse on that stream
    if (page->canReuse())
    {
        return retire(allocation);
    }

    // Case 4: Cross-stream events exist - defer until safe
    // We need to wait until all streams that used this page have completed.
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

    // Get stream from PageDesc (passed from HeapAllocDesc)
    // Note: kNoStream means no stream context, nullptr is the default CUDA stream
    CUstream stream = (desc.stream == kNoStream) ? nullptr : static_cast<CUstream>(desc.stream);

    // Try to find a reusable page in the cache first (PyTorch-style caching)
    if (m_cachingConfig.enabled)
    {
        PageImpl* cachedPage = m_pageCache.findReusable(desc.size, stream);
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

    // Set the allocation stream for the new page from PageDesc
    // Convert kNoStream to nullptr (default stream) for consistency with cache lookup (line 342)
    // This ensures pages created with kNoStream can be found when searching with kNoStream
    newPage->m_stream = (desc.stream == kNoStream) ? nullptr : desc.stream;

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
