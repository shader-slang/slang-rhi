#include "cuda-heap.h"
#include "cuda-device.h"
#include "cuda-utils.h"
#include "cuda-command.h"

#include <cstdlib>

namespace rhi::cuda {

// ============================================================================
// Environment variable configuration for caching allocator
// ============================================================================

/// Load caching configuration from environment variables.
/// Environment variables override the programmatic defaults.
/// Variables:
///   SLANG_RHI_ALLOCATOR_CACHING        - Enable/disable caching (0 or 1)
///   SLANG_RHI_ALLOCATOR_MAX_PAGES      - Max cached pages (integer)
///   SLANG_RHI_ALLOCATOR_MAX_MEMORY_MB  - Max cached memory in MB (integer)
///   SLANG_RHI_ALLOCATOR_GC_ENABLE      - Enable garbage collection (0 or 1)
///   SLANG_RHI_ALLOCATOR_GC_THRESHOLD   - GC memory threshold (float 0.0-1.0)
static void loadCachingConfigFromEnv(HeapCachingConfig& config)
{
    if (const char* val = std::getenv("SLANG_RHI_ALLOCATOR_CACHING"))
    {
        config.enabled = (std::atoi(val) != 0);
    }
    if (const char* val = std::getenv("SLANG_RHI_ALLOCATOR_MAX_PAGES"))
    {
        config.maxCachedPages = static_cast<uint32_t>(std::atoi(val));
    }
    if (const char* val = std::getenv("SLANG_RHI_ALLOCATOR_MAX_MEMORY_MB"))
    {
        config.maxCachedMemory = static_cast<Size>(std::atoi(val)) * 1024 * 1024;
    }
    if (const char* val = std::getenv("SLANG_RHI_ALLOCATOR_GC_ENABLE"))
    {
        config.enableGarbageCollection = (std::atoi(val) != 0);
    }
    if (const char* val = std::getenv("SLANG_RHI_ALLOCATOR_GC_THRESHOLD"))
    {
        config.gcMemoryThreshold = static_cast<float>(std::atof(val));
    }
}

// ============================================================================
// PageImpl - Stream tracking implementation (PyTorch-style)
// ============================================================================

HeapImpl::PageImpl::~PageImpl()
{
    // Clean up any remaining events
    for (auto& se : m_pendingEvents)
    {
        if (se.event)
        {
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
        // CUDA_SUCCESS means event completed
        // Other errors we treat as completed (something went wrong, but don't block)
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
        if (result != CUDA_ERROR_NOT_READY)
        {
            // Event completed (or errored) - clean up
            SLANG_CUDA_ASSERT_ON_FAIL(cuEventDestroy(it->event));
            it = m_pendingEvents.erase(it);
        }
        else
        {
            ++it;
        }
    }
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
            // Process any completed events first
            page->processEvents();

            // Check if page can be reused (all events completed)
            if (!page->canReuse())
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

        // Process any completed events first
        page->processEvents();

        // Check if page can be reused (all events completed)
        if (!page->canReuse())
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
    // Load environment variable overrides
    loadCachingConfigFromEnv(m_cachingConfig);
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

    // Try to find a reusable page in the cache first (PyTorch-style caching)
    if (m_cachingConfig.enabled)
    {
        PageImpl* cachedPage = m_pageCache.findReusable(desc.size, m_currentStream);
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
    newPage->m_stream = m_currentStream;

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
