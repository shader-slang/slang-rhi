#pragma once

#include "common.h"
#include <mutex>

namespace rhi {

/// Block allocator for fixed-size objects.
/// Allocates fixed-size blocks out of larger pages.
/// Thread-safe for concurrent allocations and deallocations using a mutex.
///
/// This allocator never frees pages, which means it can only
/// grow in size and never shrink.
template<typename T>
class BlockAllocator
{
public:
    /// Constructor
    /// @param blocksPerPage Number of blocks to allocate per page (default: 256).
    BlockAllocator(size_t blocksPerPage = 256)
        : m_blocksPerPage(blocksPerPage)
    {
        SLANG_RHI_ASSERT(blocksPerPage > 0);
    }

    /// Destructor - frees all pages (NOT thread safe).
    ~BlockAllocator()
    {
        Page* page = m_pageListHead;
        while (page)
        {
            Page* next = page->next;
            std::free(page);
            page = next;
        }
    }

    // Non-copyable, non-movable
    BlockAllocator(const BlockAllocator&) = delete;
    BlockAllocator& operator=(const BlockAllocator&) = delete;
    BlockAllocator(BlockAllocator&&) = delete;
    BlockAllocator& operator=(BlockAllocator&&) = delete;

    /// Allocate a block (thread safe).
    /// @return Pointer to allocated block, or nullptr if allocation fails
    T* allocate()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_freeList)
        {
            FreeBlock* block = m_freeList;
            m_freeList = block->next;
            return reinterpret_cast<T*>(block);
        }
        return allocateFromNewPageLocked();
    }

    /// Free a block (thread safe).
    /// @param ptr Pointer to block to free
    void free(T* ptr)
    {
        if (!ptr)
            return;
        FreeBlock* block = reinterpret_cast<FreeBlock*>(ptr);
        std::lock_guard<std::mutex> lock(m_mutex);
        block->next = m_freeList;
        m_freeList = block;
    }

    /// Check if a pointer is owned by this allocator (thread safe).
    /// @param ptr Pointer to check
    /// @return true if the pointer is within any page managed by this allocator
    bool owns(const void* ptr) const
    {
        if (!ptr)
            return false;
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        std::lock_guard<std::mutex> lock(m_mutex);
        Page* page = m_pageListHead;
        while (page)
        {
            uintptr_t pageStart = reinterpret_cast<uintptr_t>(page->blocks);
            uintptr_t pageEnd = pageStart + m_blocksPerPage * sizeof(Block);
            if (addr >= pageStart && addr < pageEnd)
            {
                return true;
            }
            page = page->next;
        }
        return false;
    }

    /// Reset the allocator, rebuilding the free list from all pages (NOT thread safe).
    void reset()
    {
        FreeBlock* head = nullptr;
        Page* page = m_pageListHead;
        while (page)
        {
            for (size_t i = 0; i < m_blocksPerPage; ++i)
            {
                FreeBlock* block = reinterpret_cast<FreeBlock*>(&page->blocks[i]);
                block->next = head;
                head = block;
            }
            page = page->next;
        }
        m_freeList = head;
    }

    uint32_t getNumPages() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_numPages;
    }

private:
    /// Free block - stores next pointer when block is unused.
    struct FreeBlock
    {
        FreeBlock* next;
    };

    /// A block must be large enough to hold either T or a FreeBlock.
    union Block
    {
        alignas(T) uint8_t data[sizeof(T)];
        FreeBlock freeBlock; // Used when block is free
    };

    static_assert(sizeof(Block) >= sizeof(FreeBlock*), "Block must be large enough to hold a pointer");
    static_assert(alignof(Block) >= alignof(T), "Block alignment must be sufficient for T");

    /// A page contains multiple blocks and a link to the next page.
    /// Note: blocks[1] is a flexible array member pattern - actual size is m_blocksPerPage.
    struct Page
    {
        Page* next;
        Block blocks[1];
    };

    /// Allocate a new page and return a block from it.
    /// Called while m_mutex is already held.
    T* allocateFromNewPageLocked()
    {
        // Allocate a new page
        size_t pageSize = sizeof(Page) + (m_blocksPerPage - 1) * sizeof(Block);
        Page* page = reinterpret_cast<Page*>(std::malloc(pageSize));
        if (!page)
        {
            return nullptr;
        }

        // Initialize page metadata
        page->next = m_pageListHead;
        m_pageListHead = page;
        m_numPages++;

        // Generate free list from all except first block.
        for (size_t i = 1; i < m_blocksPerPage; ++i)
        {
            FreeBlock* block = reinterpret_cast<FreeBlock*>(&page->blocks[i]);
            block->next = m_freeList;
            m_freeList = block;
        }

        // Return the first block
        return reinterpret_cast<T*>(&page->blocks[0]);
    }

    size_t m_blocksPerPage;
    FreeBlock* m_freeList{nullptr};
    mutable std::mutex m_mutex; // Protects all operations
    Page* m_pageListHead{nullptr};
    uint32_t m_numPages{0};
};

// Macro to declare block allocator support for a class
#define SLANG_RHI_DECLARE_BLOCK_ALLOCATED(ClassName, BlocksPerPage)                                                    \
public:                                                                                                                \
    void* operator new(size_t size);                                                                                   \
    void operator delete(void* ptr);                                                                                   \
                                                                                                                       \
private:                                                                                                               \
    static ::rhi::BlockAllocator<ClassName> s_allocator;

// Macro to implement block allocator operators in .cpp file
#define SLANG_RHI_IMPLEMENT_BLOCK_ALLOCATED(ClassName, BlocksPerPage)                                                  \
    ::rhi::BlockAllocator<ClassName> ClassName::s_allocator(BlocksPerPage);                                            \
                                                                                                                       \
    void* ClassName::operator new(size_t size)                                                                         \
    {                                                                                                                  \
        return s_allocator.allocate();                                                                                 \
    }                                                                                                                  \
                                                                                                                       \
    void ClassName::operator delete(void* ptr)                                                                         \
    {                                                                                                                  \
        if (!ptr)                                                                                                      \
            return;                                                                                                    \
        if (s_allocator.owns(ptr))                                                                                     \
        {                                                                                                              \
            s_allocator.free(static_cast<ClassName*>(ptr));                                                            \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            ::operator delete(ptr);                                                                                    \
        }                                                                                                              \
    }

} // namespace rhi
