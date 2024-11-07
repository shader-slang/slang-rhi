#pragma once

#include "common.h"

namespace rhi {

/// Simple paged allocator.
/// Allocates memory in pages and frees all pages on destruction.
class PagedAllocator
{
public:
    /// Default page size is 16KB (minus 16 bytes for page header).
    static constexpr size_t kDefaultPageSize = 16 * 1024 - 16;

    PagedAllocator(size_t pageSize = kDefaultPageSize)
        : m_pageSize(pageSize)
    {
    }

    ~PagedAllocator() { freePages(); }

    PagedAllocator(const PagedAllocator&) = delete;
    PagedAllocator& operator=(const PagedAllocator&) = delete;

    PagedAllocator(PagedAllocator&&) = delete;
    PagedAllocator& operator=(PagedAllocator&&) = delete;

    void* allocate(size_t size, size_t alignment = 16)
    {
        m_currentOffset = (m_currentOffset + alignment - 1) & ~(alignment - 1);
        if (!m_currentPage || m_currentOffset + size > m_currentPage->size)
        {
            m_currentPage = allocatePage(max(size, m_pageSize));
            m_currentOffset = 0;
        }
        void* result = m_currentPage->getData() + m_currentOffset;
        m_currentOffset += size;
        return result;
    }

    void reset()
    {
        freePages();
        m_currentPage = nullptr;
        m_currentOffset = 0;
    }

private:
    struct Page
    {
        Page* next;
        size_t size;
        uint8_t* getData() { return reinterpret_cast<uint8_t*>(this) + sizeof(Page); }
    };
    static_assert(sizeof(Page) == 16);

    size_t m_pageSize;
    Page* m_pages = nullptr;
    Page* m_currentPage = nullptr;
    uint32_t m_currentOffset = 0;

    Page* allocatePage(size_t size)
    {
        uint8_t* data = reinterpret_cast<uint8_t*>(std::malloc(size + sizeof(Page)));
        Page* page = reinterpret_cast<Page*>(data);
        page->size = size;
        page->next = m_pages;
        m_pages = page;
        return page;
    }

    void freePages()
    {
        Page* page = m_pages;
        while (page)
        {
            Page* next = page->next;
            std::free(page);
            page = next;
        }
        m_pages = nullptr;
    }
};

} // namespace rhi
