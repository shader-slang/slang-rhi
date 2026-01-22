#pragma once

#include "cuda-base.h"
#include "../heap.h"

#include <list>
#include <vector>

namespace rhi::cuda {

class HeapImpl : public Heap
{
public:
    // Selected based on (https://docs.nvidia.com/cuda/cuda-c-programming-guide/index.html#alignment)
    // Highest alignment requirement in CUDA is 128, for optimal memcpy performance.
    static const Size kAlignment = 128;

    struct PendingFree
    {
        HeapAlloc allocation;
        uint64_t submitIndex;
    };

    class PageImpl : public Heap::Page
    {
    public:
        /// Event recorded on a stream to track when that stream is done with this page.
        /// Following PyTorch model: events only created when stream != m_stream (allocation stream).
        struct StreamEvent
        {
            CUstream stream;
            CUevent event;
        };

        PageImpl(Heap* heap, const PageDesc& desc, CUdeviceptr cudaMemory)
            : Heap::Page(heap, desc)
            , m_cudaMemory(cudaMemory)
        {
        }

        ~PageImpl();

        DeviceAddress offsetToAddress(Size offset) override { return DeviceAddress(m_cudaMemory + offset); }

        /// Record that this page is being used by a stream different from m_stream.
        /// Creates a CUDA event and records it on the stream for synchronization tracking.
        /// Following PyTorch model: events are only added when stream != m_stream.
        void recordStreamUse(void* stream) override;

        /// Check if this page can be reused (all pending stream events completed).
        /// Queries CUDA event completion status for all pending events.
        bool canReuse() const override;

        /// Process completed events and clean up.
        /// Removes events that have completed from m_pendingEvents.
        void processEvents() override;

        /// Process events and check if page can be reused in a single pass.
        /// More efficient than processEvents() + canReuse() and avoids race condition
        /// where events complete between the two calls.
        /// @return true if the page can be safely reused (all events completed)
        bool processEventsAndCheckReuse();

        /// Called when this page is used for an allocation.
        /// Records cross-stream usage if the passed stream differs from the page's stream.
        /// @param stream The stream context for this allocation (from HeapAllocDesc::stream)
        void notifyUse(void* stream) override;

        CUdeviceptr m_cudaMemory;

        /// Events tracking streams that have used this page (for multi-stream synchronization).
        /// Only contains events for streams different from m_stream.
        std::vector<StreamEvent> m_pendingEvents;
    };

    // ============================================================================
    // Page Cache - PyTorch-style caching allocator
    // ============================================================================

    /// Cache of freed pages for reuse, organized by size tier.
    /// Pages are not actually freed to CUDA until garbage collection.
    struct PageCache
    {
        /// Find a reusable page of the given size that can be used on the target stream.
        /// @param size The required page size
        /// @param stream The stream that will use this page (for synchronization check)
        /// @return A reusable page, or nullptr if none found
        PageImpl* findReusable(Size size, CUstream stream);

        /// Insert a page into the cache (called when page is "freed")
        void insert(PageImpl* page);

        /// Remove a page from the cache
        void remove(PageImpl* page);

        /// Release all cached pages back to CUDA (garbage collection)
        void releaseAll(DeviceImpl* device, MemoryType memType);

        /// Get total cached memory size
        Size getCachedSize() const;

        /// Cached pages organized by size tier (8MB, 64MB, 256MB, etc.)
        std::list<PageImpl*> m_cachedPages;
    };

    HeapImpl(Device* device, const HeapDesc& desc);
    ~HeapImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL free(HeapAlloc allocation) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL flush() override;

    virtual Result allocatePage(const PageDesc& desc, Page** outPage) override;
    virtual Result freePage(Page* page) override;

    // Alignments
    virtual Result fixUpAllocDesc(HeapAllocDesc& desc) override;

    std::list<PendingFree> m_pendingFrees;

    /// Page cache for reuse (PyTorch-style caching allocator)
    PageCache m_pageCache;

    /// Caching configuration (copied from HeapDesc at creation)
    HeapCachingConfig m_cachingConfig;
};

} // namespace rhi::cuda
