#pragma once

#include <slang-rhi.h>
#include "core/common.h"
#include "core/offset-allocator.h"
#include "rhi-shared-fwd.h"

#include <memory>

namespace rhi {

/// Device-owned, mapped upload pages with individually reclaimable, aligned slices.
/// A slice is retained by prepared bindings and recorded commands, never recycled on submission.
class PersistentBufferPool
{
    struct Page;

public:
    PersistentBufferPool();
    ~PersistentBufferPool();
    PersistentBufferPool(const PersistentBufferPool&) = delete;
    PersistentBufferPool& operator=(const PersistentBufferPool&) = delete;

    class Allocation : public RefObject
    {
    public:
        ~Allocation();
        Allocation(const Allocation&) = delete;
        Allocation& operator=(const Allocation&) = delete;
        // Retain the allocation, not just its buffer, to keep this range reserved.
        Buffer* getBuffer() const;
        Offset getOffset() const;
        // Includes alignment padding owned by this slice.
        Size getSize() const { return m_size; }
        void* getMappedData() const;

    private:
        Allocation(PersistentBufferPool* pool, Page* page, OffsetAllocator::Allocation region, Size size);
        // Pages held by the device have weak device references; live slices keep the device alive.
        RefPtr<Device> m_device;
        PersistentBufferPool* m_pool;
        Page* m_page;
        OffsetAllocator::Allocation m_region;
        Size m_size;
        friend class PersistentBufferPool;
    };

    struct Stats
    {
        size_t pageCount = 0;
        Size capacity = 0;
        Size used = 0;
        uint64_t allocationCount = 0;
        uint64_t pageAllocationCount = 0;
    };

    void initialize(Device* device, Size alignment, Size pageSize = 64 * 1024, Size maxRetainedSize = 64 * 1024);
    // Requires no live slices; call before destroying the backend device/queue.
    void release();
    Result allocate(Size size, RefPtr<Allocation>& outAllocation);
    Stats getStats() const;
    Size getAlignment() const { return m_alignment; }

private:
    Device* m_device = nullptr;
    Size m_alignment = 0;
    Size m_pageSize = 0;
    Size m_maxRetainedSize = 0;
    Size m_emptyCapacity = 0;
    Stats m_stats;
    std::vector<std::unique_ptr<Page>> m_pages;
    Page* m_currentPage = nullptr;
    mutable std::mutex m_mutex;

    Result createPage(Size capacity, Page*& outPage);
    void free(Allocation* allocation);
    void trimEmptyPages();
    void destroyPage(Page* page);
};

} // namespace rhi
