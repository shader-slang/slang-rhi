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
        MetaData metadata;
    };

    struct Allocation
    {
        std::list<Node>::iterator node;
        Buffer* buffer;
        int page;
        int flags;
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

        Offset getOffset() const
        {
            return 0;
             //m_allocation.node->offset;
        }

    private:
        StagingHeap* m_heap;
        Allocation m_allocation;
    };

    class Page : public RefObject
    {
    public:

        Page(int id, RefPtr<Buffer> buffer)
            : m_id(id)
            , m_buffer(buffer) {};


        int getId() const { return m_id; }
        RefPtr<Buffer> getBuffer() const { return m_buffer; }

    private:
        int m_id;
        RefPtr<Buffer> m_buffer;
    };

    // Initialize with device pointer.
    void initialize(Device* device);

    // Allocate block of memory and wrap in a ref counted handle that automatically
    // frees the allocation when handle is freed.
    RefPtr<Handle> allocHandle(size_t size, size_t alignment, MetaData metadata);

    // Allocate a block of memory.
    Allocation alloc(size_t size, size_t alignment);

    // Free existing allocation.
    void free(Allocation allocation);

private:
    Device* m_device;
    int m_next_page_id;
    std::unordered_map<int, RefPtr<Page>> m_pages;

    RefPtr<Page> allocPage(size_t size);
};

}
