#pragma once

#include <slang-rhi.h>

#include "core/common.h"
#include "reference.h"

#include "rhi-shared-fwd.h"

#include <list>
#include <unordered_map>
#include <mutex>
#include <thread>

namespace rhi {

class StagingHeap : public RefObject
{
public:
    // Arbitrary meta data that sits within heap node to store extra info about allocation.
    struct MetaData
    {
        int use;
    };

    // Heap node, represents a free or allocated range of memory in a page.
    struct Node
    {
        Offset offset;
        Size size;
        bool free;
        MetaData metadata;
    };


    // Memory page within heap.
    class Page : public RefObject
    {
    public:
        Page(int id, RefPtr<Buffer> buffer);

        // Allocate a node from the page heap.
        bool allocNode(
            Size size,
            StagingHeap::MetaData metadata,
            std::thread::id lock_to_thread,
            std::list<Node>::iterator& res
        );

        // Free a node.
        void freeNode(std::list<Node>::iterator node);

        // Get page id.
        int getId() const { return m_id; }

        // Get device buffer mapped to this page.
        Buffer* getBuffer() const { return m_buffer.get(); }

        // Get total capacity of the page.
        size_t getCapacity() const { return m_totalCapacity; }

        // Get total used in the page.
        size_t getUsed() const { return m_totalUsed; }

        // Get mapped address
        uint8_t* getMapped() const { return (uint8_t*)m_mapped; }

        // Get thread id page is locked to (if any)
        std::thread::id getLockedToThread() const { return m_locked_to_thread; }

        // Map page
        Result map(Device* device);

        // Unmap page
        Result unmap(Device* device);

        // Debug check consistency of page's heap.
        void checkConsistency();

    private:
        int m_id;
        RefPtr<Buffer> m_buffer;
        std::list<Node> m_nodes;
        size_t m_totalCapacity = 0;
        size_t m_totalUsed = 0;
        void* m_mapped = nullptr;
        std::thread::id m_locked_to_thread;
    };

    // Memory allocation within heap.
    struct Allocation
    {
        std::list<Node>::iterator node;
        Page* page;

        Offset getOffset() const { return node->offset; }
        Size getSize() const { return node->size; }
        Page* getPage() const { return page; }
        int getPageId() const { return page->getId(); }
        Buffer* getBuffer() const { return page->getBuffer(); }
        const MetaData& getMetaData() const { return node->metadata; }
    };

    // Handle to a memory allocation that automatically frees the allocation when handle is freed.
    class Handle : public RefObject
    {
    public:
        Handle(StagingHeap* heap, Allocation allocation)
            : m_heap(heap)
            , m_allocation(allocation)
        {
        }
        ~Handle() { m_heap->free(m_allocation); }

        const Allocation& getAllocation() const { return m_allocation; }
        Offset getOffset() const { return m_allocation.getOffset(); }
        Size getSize() const { return m_allocation.getSize(); }
        Page* getPage() const { return m_allocation.page; }
        int getPageId() const { return m_allocation.getPageId(); }
        Buffer* getBuffer() const { return m_allocation.getBuffer(); }
        const MetaData& getMetaData() const { return m_allocation.getMetaData(); }

        Result map(void** outAddress) { return m_heap->map(m_allocation, outAddress); }
        Result unmap() { return m_heap->unmap(m_allocation); }

    private:
        StagingHeap* m_heap;
        Allocation m_allocation;
    };

    // Initialize with device pointer.
    void initialize(Device* device, Size pageSize, MemoryType memoryType);

    // Attempt to cleanup and check no allocations remain
    void release();

    // Allocate block of memory and wrap in a ref counted handle that automatically
    // frees the allocation when handle is freed.
    Result allocHandle(size_t size, MetaData metadata, Handle** outHandle);

    // Allocate a block of memory.
    Result alloc(size_t size, MetaData metadata, Allocation* outAllocation);

    // Allocate/store a block of data in the heap and wrap in a ref counted
    // handle that automatically frees the allocation when handle is freed.
    Result stageHandle(const void* data, size_t size, MetaData metadata, Handle** outHandle);

    // Allocate/store a block of data in the heap.
    Result stage(const void* data, size_t size, MetaData metadata, Allocation* outAllocation);

    // Free existing allocation.
    void free(Allocation allocation);

    // Debug check consistency of heap
    void checkConsistency();

    // Get total allocated pages.
    size_t getNumPages()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pages.size();
    }

    // Get total capacity of heap.
    Size getCapacity() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_totalCapacity;
    }

    // Get current usage in heap.
    Size getUsed() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_totalUsed;
    }

    // Get alignment of heap.
    Size getAlignment() const { return m_alignment; }

    // Get default page size.
    Size getPageSize() const { return m_pageSize; }

    // Align a size to that of heap allocations.
    Size alignUp(Size value) { return (value + m_alignment - 1) / m_alignment * m_alignment; }

    // Used by testing system to change whether pages stay mapped
    void testOnlySetKeepPagesMapped(bool keepPagesMapped) { m_keepPagesMapped = keepPagesMapped; }

    // Map memory for allocation (if not mapped already) and return ptr to it
    Result map(const Allocation& allocation, void** outAddress);

    // Unmap memory for allocation if necessary (for heap that keeps pages mapped, this is noop)
    Result unmap(const Allocation& allocation);

private:
    Device* m_device = nullptr;
    int m_nextPageId = 1;
    Size m_totalCapacity = 0;
    Size m_totalUsed = 0;
    Size m_alignment = 1024;
    Size m_pageSize = 16 * 1024 * 1024;
    bool m_keepPagesMapped = true;
    std::unordered_map<int, RefPtr<Page>> m_pages;
    MemoryType m_memoryType;
    mutable std::mutex m_mutex;

    Result allocHandleInternal(size_t size, MetaData metadata, Handle** outHandle);

    Result allocInternal(size_t size, MetaData metadata, Allocation* outAllocation);

    Result allocPage(size_t size, StagingHeap::Page** outPage);

    void freePage(StagingHeap::Page* page);

    void releaseAllFreePages();
};

} // namespace rhi
