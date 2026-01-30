#include "cuda-heap.h"
#include "cuda-device.h"
#include "cuda-utils.h"
#include "cuda-command.h"

namespace rhi::cuda {

// ============================================================================
// CUDA Heap Allocator - Design Overview
// ============================================================================
//
// This allocator exploits CUDA's stream execution model for efficient memory reuse.
//
// KEY INSIGHT: CUDA streams execute operations in FIFO order. If memory is freed
// and reallocated on the same stream, the new work using that memory is guaranteed
// to execute AFTER the previous work completes. No explicit synchronization needed.
//
// SAME-STREAM IMMEDIATE REUSE:
//   1. Page allocated on stream A, used by GPU work on stream A
//   2. Page freed (but not returned to CUDA)
//   3. Page reallocated on stream A for new work
//   4. CUDA guarantees step 1's work completes before step 3's work starts
//
// FREE PATH:
//   heap->free(allocation)
//       │
//       ├─► Same stream + no cross-stream events ──► IMMEDIATE retire
//       │
//       └─► Otherwise ──► m_pendingFrees (deferred until GPU done)
//
// CROSS-STREAM SYNCHRONIZATION:
//   When a page is used by a different stream than it was allocated on, we record
//   a CUDA event on that stream. Before reusing the page, we check all events
//   have completed. This is the only case requiring explicit synchronization.
//
// PAGE CACHING:
//   Freed pages go to a cache instead of cuMemFree(). New allocations check the
//   cache first, preferring pages from the same stream (no sync needed) over
//   pages from different streams (may need event waits).
//
// LAZY EVENTS:
//   For single-stream workloads (the common case), we avoid creating CUDA events
//   entirely. Command buffer retirement uses cuStreamQuery() instead, which is
//   a non-blocking check that the stream is idle.
//
// These optimizations are inspired by PyTorch's CUDACachingAllocator.
// ============================================================================

// ============================================================================
// PageImpl - Stream tracking implementation
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

    // Only add events when stream differs from allocation stream.
    // nullptr (default stream) still needs tracking if it differs from m_stream.
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
    // If no stream context provided (kInvalidCUDAStream sentinel), nothing to track
    if (stream == kInvalidCUDAStream)
    {
        return;
    }

    CUstream currentStream = static_cast<CUstream>(stream);

    // Lazy stream assignment - adopt first stream that uses this page
    if (m_stream == nullptr)
    {
        m_stream = currentStream;
        return;
    }

    // Record cross-stream usage for proper synchronization
    if (m_stream != currentStream)
    {
        recordStreamUse(currentStream);
    }
}

// ============================================================================
// PageCache
// ============================================================================

HeapImpl::PageImpl* HeapImpl::PageCache::findReusable(Size size, CUstream stream)
{
    // First pass: prefer same-stream pages (no sync needed, see design overview)
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

    // Immediate reuse when safe - CUDA stream FIFO ordering guarantees safety

    // Case 1: No stream assignment - page never used by GPU.
    // Note: allocatePage() converts kInvalidCUDAStream to nullptr, so we check for nullptr here.
    if (page->m_stream == nullptr)
    {
        return retire(allocation);
    }

    // Case 2: Queue is completely idle - all GPU work is done
    if (deviceImpl->m_queue->m_lastFinishedID == deviceImpl->m_queue->m_lastSubmittedID)
    {
        return retire(allocation);
    }

    // Case 3: No cross-stream events - same-stream reuse is safe
    if (page->canReuse())
    {
        return retire(allocation);
    }

    // Case 4: Cross-stream events exist - defer until all streams complete
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
    // Note: kInvalidCUDAStream means no stream context, nullptr is the default CUDA stream
    CUstream stream = (desc.stream == kInvalidCUDAStream) ? nullptr : static_cast<CUstream>(desc.stream);

    // Try to find a reusable page in the cache first
    if (m_cachingConfig.enabled)
    {
        PageImpl* cachedPage = m_pageCache.findReusable(desc.size, stream);
        if (cachedPage)
        {
            // Reusing cached page - keeps its original stream ownership
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
    // Convert kInvalidCUDAStream to nullptr (default stream) for consistency with cache lookup (line 342)
    // This ensures pages created with kInvalidCUDAStream can be found when searching with kInvalidCUDAStream
    newPage->m_stream = (desc.stream == kInvalidCUDAStream) ? nullptr : desc.stream;

    *outPage = newPage;
    return SLANG_OK;
}

Result HeapImpl::freePage(Page* page_)
{
    PageImpl* page = static_cast<PageImpl*>(page_);

    // Cache for reuse instead of freeing to CUDA
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
