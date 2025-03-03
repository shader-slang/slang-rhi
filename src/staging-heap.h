#pragma once

#include <slang-rhi.h>

#include "core/common.h"
#include "reference.h"

#include "rhi-shared-fwd.h"

#include <list>
#include <unordered_map>

namespace rhi {

class StagingHeap: public RefObject
{
public:
    struct MetaData
    {
        int use;
    };

    struct Node
    {
        Offset offset;
        Size size;
        bool free;
        int pageid;
        MetaData metadata;
    };

    struct Allocation
    {
        std::list<Node>::iterator node;
        Buffer* buffer;

        Offset getOffset() const { return node->offset; }
        Size getSize() const { return node->size; }
        int getPageId() const { return node->pageid; }
        const MetaData& getMetaData() const { return node->metadata; }

    };



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

    class Page : public RefObject
    {
    public:

        Page(int id, RefPtr<Buffer> buffer);

        bool allocNode(Size size, StagingHeap::MetaData metadata, std::list<Node>::iterator& res);

        void freeNode(std::list<Node>::iterator node);

        int getId() const { return m_id; }
        RefPtr<Buffer> getBuffer() const { return m_buffer; }

        size_t getCapacity() const { return m_total_capacity; }
        size_t getUsed() const { return m_total_used; }

        void checkConsistency();

    private:
        int m_id;
        RefPtr<Buffer> m_buffer;
        std::list<Node> m_nodes;
        size_t m_total_capacity = 0;
        size_t m_total_used = 0;
    };

    // Initialize with device pointer.
    void initialize(Device* device);

    // Allocate block of memory and wrap in a ref counted handle that automatically
    // frees the allocation when handle is freed.
    RefPtr<Handle> allocHandle(size_t size, MetaData metadata);

    // Allocate a block of memory.
    Allocation alloc(size_t size, MetaData metadata);

    // Free existing allocation.
    void free(Allocation allocation);

    // Get total allocated pages.
    size_t getNumPages() { return m_pages.size(); }

    // Debug check consistency of heap
    void checkConsistency();

    Size getCapacity() const { return m_total_capacity; }
    Size getUsed() const { return m_total_used; }
    Size getAlignment() const { return m_alignment; }

    Size alignUp(Size value)
    {
        return (value + m_alignment - 1) / m_alignment * m_alignment;
    }

private:
    Device* m_device = nullptr;
    int m_next_page_id = 1;
    Size m_total_capacity = 0;
    Size m_total_used = 0;
    Size m_alignment = 1024;
    Size m_page_size = 16 * 1024 * 1024;
    std::unordered_map<int, RefPtr<Page>> m_pages;

    RefPtr<Page> allocPage(size_t size);
};

}
