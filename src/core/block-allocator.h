#pragma once

#include "common.h"
#include <atomic>
#include <mutex>
#include <cstring>

namespace rhi {

/// Lockless block allocator for fixed-size objects.
/// Allocates fixed-size blocks out of larger pages.
/// Uses a lockless free list for allocation/deallocation.
/// Thread-safe for concurrent allocations and deallocations.
///
/// This allocator never frees pages, which allows
/// it to be completely lock-free, but means it can only
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
        Page* page = m_pageListHead.load(std::memory_order_acquire);
        while (page)
        {
            Page* next = page->next.load(std::memory_order_acquire);
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
        m_totalBlocksAllocated++;

        // std::lock_guard<std::mutex> lock(m_pageMutex);
        FreeBlock* block = m_freeList.load(std::memory_order_acquire);
        while (block)
        {
            FreeBlock* next = block->next.load(std::memory_order_acquire);
            if (m_freeList.compare_exchange_weak(block, next, std::memory_order_release, std::memory_order_acquire))
            {
                // Successfully popped from free list
                return reinterpret_cast<T*>(block);
            }
        }
        return allocateFromNewPage();
    }

    /// Deallocate a block (thread safe).
    /// @param ptr Pointer to block to deallocate
    void deallocate(T* ptr)
    {
        // std::lock_guard<std::mutex> lock(m_pageMutex);
        if (!ptr)
            return;
        FreeBlock* block = reinterpret_cast<FreeBlock*>(ptr);
        FreeBlock* head = m_freeList.load(std::memory_order_acquire);
        do
        {
            block->next.store(head, std::memory_order_release);
        }
        while (!m_freeList.compare_exchange_weak(head, block, std::memory_order_release, std::memory_order_acquire));

        m_totalBlocksAllocated--;
    }

    /// Check if a pointer is owned by this allocator (thread safe).
    /// @param ptr Pointer to check
    /// @return true if the pointer is within any page managed by this allocator
    bool owns(const void* ptr) const
    {
        if (!ptr)
            return false;
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        Page* page = m_pageListHead.load(std::memory_order_acquire);
        while (page)
        {
            uintptr_t pageStart = reinterpret_cast<uintptr_t>(page->blocks);
            uintptr_t pageEnd = pageStart + page->blockCount * sizeof(Block);
            if (addr >= pageStart && addr < pageEnd)
            {
                return true;
            }
            page = page->next.load(std::memory_order_acquire);
        }
        return false;
    }

    /// Reset the allocator, rebuilding the free list from all pages (NOT thread safe).
    void reset()
    {
        FreeBlock* head = nullptr;
        Page* page = m_pageListHead.load(std::memory_order_acquire);
        while (page)
        {
            for (size_t i = 0; i < page->blockCount; ++i)
            {
                FreeBlock* block = reinterpret_cast<FreeBlock*>(&page->blocks[i]);
                block->next.store(head, std::memory_order_release);
                head = block;
            }
            page = page->next.load(std::memory_order_acquire);
        }
        m_freeList.store(head, std::memory_order_release);
    }

    uint32_t getNumPages() const
    {
        std::lock_guard<std::mutex> lock(m_pageMutex);
        return m_numPages;
    }

private:
    /// Free block - stores next pointer when block is unused.
    struct FreeBlock
    {
        std::atomic<FreeBlock*> next;
    };

    /// A block must be large enough to hold either T or a FreeBlock.
    union Block
    {
        alignas(T) uint8_t data[sizeof(T)];
        FreeBlock freeBlock; // Used when block is free
    };

    /// A page contains multiple blocks and a link to the next page
    struct Page
    {
        std::atomic<Page*> next;
        size_t blockCount;
        Block blocks[1];
    };

    static_assert(sizeof(Block) >= sizeof(FreeBlock*), "Block must be large enough to hold a pointer");
    static_assert(alignof(Block) >= alignof(T), "Block alignment must be sufficient for T");

    /// Allocate a new page and return a block from it.
    /// This is protected by a mutex so multiple new pages don't
    /// get allocated at once.
    T* allocateFromNewPage()
    {
        std::lock_guard<std::mutex> lock(m_pageMutex);

        // Check free list again in case another thread allocated a page
        // whilst we waited for the mutex.
        FreeBlock* block = m_freeList.load(std::memory_order_acquire);
        if (block)
        {
            FreeBlock* next = block->next.load(std::memory_order_acquire);
            if (m_freeList.compare_exchange_strong(block, next, std::memory_order_release, std::memory_order_acquire))
            {
                return reinterpret_cast<T*>(block);
            }
        }

        // Allocate a new page
        size_t pageSize = sizeof(Page) + (m_blocksPerPage - 1) * sizeof(Block);
        Page* page = reinterpret_cast<Page*>(std::malloc(pageSize));
        if (!page)
        {
            return nullptr;
        }

        // Initialize page metadata
        page->blockCount = m_blocksPerPage;
        page->next.store(nullptr, std::memory_order_release);

        // Atomically prepend page to linked list of pages.
        Page* oldHead = m_pageListHead.load(std::memory_order_acquire);
        do
        {
            page->next.store(oldHead, std::memory_order_release);
        }
        while (
            !m_pageListHead.compare_exchange_weak(oldHead, page, std::memory_order_release, std::memory_order_acquire)
        );
        m_numPages++;


        // Generate free list from all except first block.
        FreeBlock* localChain = reinterpret_cast<FreeBlock*>(&page->blocks[1]);
        FreeBlock* current = localChain;
        for (size_t i = 2; i < m_blocksPerPage; ++i)
        {
            FreeBlock* nextBlock = reinterpret_cast<FreeBlock*>(&page->blocks[i]);
            current->next.store(nextBlock, std::memory_order_release);
            current = nextBlock;
        }
        current->next.store(nullptr, std::memory_order_release);

        // Locklessly append local chain to global free list
        FreeBlock* oldFreeHead = m_freeList.load(std::memory_order_acquire);
        do
        {
            current->next.store(oldFreeHead, std::memory_order_release); // Link the tail to the current head
        }
        while (
            !m_freeList
                 .compare_exchange_weak(oldFreeHead, localChain, std::memory_order_release, std::memory_order_acquire)
        );

        // Return the first block
        return reinterpret_cast<T*>(&page->blocks[0]);
    }

    size_t m_blocksPerPage;
    std::atomic<FreeBlock*> m_freeList{nullptr};
    mutable std::mutex m_pageMutex; // Only for page allocation
    std::atomic<Page*> m_pageListHead{nullptr};
    std::atomic<uint32_t> m_totalBlocksAllocated{0};
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
            s_allocator.deallocate(static_cast<ClassName*>(ptr));                                                      \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            ::operator delete(ptr);                                                                                    \
        }                                                                                                              \
    }

} // namespace rhi
