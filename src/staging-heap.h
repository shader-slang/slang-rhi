#pragma once

#include <slang-rhi.h>

#include "core/common.h"
#include "reference.h"

#include "rhi-shared-fwd.h"

#include <list>
#include <unordered_map>

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
        int pageid;
        MetaData metadata;
    };

    // Memory allocation within heap.
    struct Allocation
    {
        std::list<Node>::iterator node;
        Buffer* buffer;

        Offset getOffset() const { return node->offset; }
        Size getSize() const { return node->size; }
        int getPageId() const { return node->pageid; }
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

        Buffer* getBuffer() const { return m_allocation.buffer; }

        Offset getOffset() const { return m_allocation.getOffset(); }
        Size getSize() const { return m_allocation.getSize(); }
        int getPageId() const { return m_allocation.getPageId(); }
        const MetaData& getMetaData() const { return m_allocation.getMetaData(); }

    private:
        StagingHeap* m_heap;
        Allocation m_allocation;
    };

    // Memory page within heap.
    class Page : public RefObject
    {
    public:
        Page(int id, RefPtr<Buffer> buffer);

        // Allocate a node from the page heap.
        bool allocNode(Size size, StagingHeap::MetaData metadata, std::list<Node>::iterator& res);

        // Free a node.
        void freeNode(std::list<Node>::iterator node);

        // Get page id.
        int getId() const { return m_id; }

        // Get device buffer mapped to this page.
        RefPtr<Buffer> getBuffer() const { return m_buffer; }

        // Get total capacity of the page.
        size_t getCapacity() const { return m_totalCapacity; }

        // Get total used in the page.
        size_t getUsed() const { return m_totalUsed; }

        // Debug check consistency of page's heap.
        void checkConsistency();

    private:
        int m_id;
        RefPtr<Buffer> m_buffer;
        std::list<Node> m_nodes;
        size_t m_totalCapacity = 0;
        size_t m_totalUsed = 0;
    };

    // Initialize with device pointer.
    void initialize(Device* device, Size pageSize);

    // Attempt to cleanup and check no allocations remain
    void release();

    // Immediately free all pages
    void releaseAllFreePages();

    // Allocate block of memory and wrap in a ref counted handle that automatically
    // frees the allocation when handle is freed.
    Result allocHandle(size_t size, MetaData metadata, Handle** outHandle);

    // Allocate a block of memory.
    Result alloc(size_t size, MetaData metadata, Allocation* outAllocation);

    // Free existing allocation.
    void free(Allocation allocation);

    // Debug check consistency of heap
    void checkConsistency();

    // Get total allocated pages.
    size_t getNumPages() { return m_pages.size(); }

    // Get total capacity of heap.
    Size getCapacity() const { return m_totalCapacity; }

    // Get current usage in heap.
    Size getUsed() const { return m_totalUsed; }

    // Get alignment of heap.
    Size getAlignment() const { return m_alignment; }

    // Get default page size.
    Size getPageSize() const { return m_pageSize; }

    // Align a size to that of heap allocations.
    Size alignUp(Size value) { return (value + m_alignment - 1) / m_alignment * m_alignment; }

private:
    Device* m_device = nullptr;
    int m_nextPageId = 1;
    Size m_totalCapacity = 0;
    Size m_totalUsed = 0;
    Size m_alignment = 1024;
    Size m_pageSize = 16 * 1024 * 1024;
    std::unordered_map<int, RefPtr<Page>> m_pages;

    Result allocPage(size_t size, StagingHeap::Page** outPage);
};

} // namespace rhi
