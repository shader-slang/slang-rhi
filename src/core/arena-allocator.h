#pragma once

#include "common.h"

namespace rhi {

/// Simple arena allocator.
/// Allocates memory in pages and frees all pages on destruction.
class ArenaAllocator
{
public:
    /// Default page size is 1MB.
    static constexpr size_t kDefaultPageSize = 1024 * 1024;

    ArenaAllocator(size_t pageSize = kDefaultPageSize)
        : m_pageSize(pageSize)
    {
    }

    ~ArenaAllocator() { freePages(); }

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    ArenaAllocator(ArenaAllocator&&) = delete;
    ArenaAllocator& operator=(ArenaAllocator&&) = delete;

    void* allocate(size_t size, size_t alignment = 16)
    {
        m_currentOffset = (m_currentOffset + alignment - 1) & ~(alignment - 1);
        if (!m_currentPage || m_currentOffset + size > m_currentPage->size)
        {
            if (!m_currentPage)
            {
                m_pages = m_currentPage = allocatePage(m_pageSize);
            }
            else
            {
                if (!m_currentPage->next)
                {
                    m_currentPage->next = allocatePage(max(size, m_pageSize));
                }
                m_currentPage = m_currentPage->next;
            }
            m_currentOffset = 0;
        }
        void* result = m_currentPage->getData() + m_currentOffset;
        m_currentOffset += size;
        return result;
    }

    template<typename T>
    T* allocate(size_t count = 1)
    {
        static_assert(std::is_pod_v<T>, "T must be POD");
        return reinterpret_cast<T*>(allocate(count * sizeof(T), alignof(T)));
    }

    void reset()
    {
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
        uint8_t* data = reinterpret_cast<uint8_t*>(std::malloc(size));
        Page* page = reinterpret_cast<Page*>(data);
        page->next = nullptr;
        page->size = size - sizeof(Page);
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
