#pragma once

#include "common.h"

namespace rhi {

/// Simple arena allocator.
/// Allocates memory in pages and allows reuse of memory by resetting the allocator.
/// All pages are freed when the allocator is destroyed.
/// The allocator is not thread-safe.
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

    /// Allocate memory of the given size with the given alignment.
    /// Alignment must be a power of 2.
    void* allocate(size_t size, size_t alignment = 16)
    {
        m_pos = (m_pos + alignment - 1) & ~(alignment - 1);
        if (!m_page || m_pos + size > m_page->end)
        {
            if (!m_page)
            {
                m_pages = m_page = allocatePage(m_pageSize);
            }
            else
            {
                if (!m_page->next)
                {
                    m_page->next = allocatePage(max(size + alignment, m_pageSize));
                }
                m_page = m_page->next;
            }
            m_pos = (m_page->begin + alignment - 1) & ~(alignment - 1);
        }
        void* result = (void*)m_pos;
        m_pos += size;
        SLANG_RHI_ASSERT(result != nullptr);
        SLANG_RHI_ASSERT(((uintptr_t)result & (alignment - 1)) == 0);
        return result;
    }

    /// Allocate memory for the given number of elements of type T.
    template<typename T>
    T* allocate(size_t count = 1)
    {
        static_assert(std::is_pod_v<T>, "T must be POD");
        return reinterpret_cast<T*>(allocate(count * sizeof(T), alignof(T)));
    }

    /// Reset the allocator.
    void reset()
    {
        m_page = m_pages;
        m_pos = m_page ? m_page->begin : 0;
    }

private:
    struct Page
    {
        Page* next;
        size_t size;
        uintptr_t begin;
        uintptr_t end;
    };
    static_assert(sizeof(Page) == 32);

    size_t m_pageSize;
    Page* m_pages = nullptr;
    Page* m_page = nullptr;
    uintptr_t m_pos = 0;

    Page* allocatePage(size_t size)
    {
        uint8_t* data = reinterpret_cast<uint8_t*>(std::malloc(size));
        Page* page = reinterpret_cast<Page*>(data);
        page->next = nullptr;
        page->size = size - sizeof(Page);
        page->begin = reinterpret_cast<uintptr_t>(data) + sizeof(Page);
        page->end = page->begin + page->size;
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
