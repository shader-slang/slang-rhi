#pragma once

#include "common.h"
#include <atomic>
#include <mutex>

namespace rhi {

/// Block allocator for fixed-size objects.
/// Allocates fixed-size blocks of sizeof(T) out of larger pages.
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

/// Reference-counting wrapper over BlockAllocator. This is a workaround that
/// allows loading slang-rhi multiple times in different shared libraries in the
/// same process. Reference-counting avoids multiple initialization and
/// deinitialization in case slang-rhi is loaded using dlopen(..., RTLD_GLOBAL).
///
/// Note that this class cannot be protected by std::mutex, since the mutex
/// itself would be subject to multiple initialization and deinitialization. So,
/// we'll have to deal with dlopen/dlclose race by using a low-level spinlock.
/// Only constructor and destructor need to be protected, since these are the
/// potential bounds for the lifespan of the underlying allocator.
///
/// See https://github.com/shader-slang/slang/issues/10785 for details.
template<typename T>
class RefCountedBlockAllocator
{
public:
    /// Constructor
    ///
    /// First reference initializes the underlying BlockAllocator
    RefCountedBlockAllocator(size_t blocksPerPage)
    {
        ScopedSpinlockGuard lock{s_allocatorLock};

        SLANG_RHI_ASSERT(s_refcount >= 0);
        if (s_refcount == 0)
        {
            s_allocator = new BlockAllocator<T>(blocksPerPage);
        }
        ++s_refcount;
    }

    /// Destructor
    ///
    /// Last reference deletes the underlying BlockAllocator
    ~RefCountedBlockAllocator()
    {
        ScopedSpinlockGuard lock{s_allocatorLock};

        SLANG_RHI_ASSERT(s_refcount > 0);
        --s_refcount;
        if (s_refcount == 0)
        {
            delete s_allocator;
            s_allocator = nullptr;
        }
    }

    // non-copyable, non-movable
    RefCountedBlockAllocator(const RefCountedBlockAllocator&) = delete;
    RefCountedBlockAllocator& operator=(const RefCountedBlockAllocator&) & = delete;
    RefCountedBlockAllocator(RefCountedBlockAllocator&&) = delete;
    RefCountedBlockAllocator& operator=(RefCountedBlockAllocator&&) & = delete;

    /// Allocate a block (thread safe).
    /// @return Pointer to allocated block, or nullptr if allocation fails
    T* allocate()
    {
        SLANG_RHI_ASSERT(s_refcount > 0);
        return s_allocator->allocate();
    }

    /// Free a block (thread safe).
    /// @param ptr Pointer to block to free
    void free(T* ptr)
    {
        SLANG_RHI_ASSERT(s_refcount > 0);
        s_allocator->free(ptr);
    }

    /// Return the underlying allocator
    BlockAllocator<T>& getUnderlyingAllocator()
    {
        SLANG_RHI_ASSERT(s_refcount > 0);
        return *s_allocator;
    }

private:
    static int s_refcount;
    static std::atomic<bool> s_allocatorLock; // spinlock state
    static BlockAllocator<T>* s_allocator;    // the underlying allocator

    /// Scoped spin lock guard, similar to std::lock_guard
    ///
    /// This implementation is not optimized for contention. It only provides
    /// thread safety for the rare case of dlopen/dlclose race.
    struct ScopedSpinlockGuard
    {
        std::atomic<bool>& m_spinlock;

        /// Acquire the lock
        ScopedSpinlockGuard(std::atomic<bool>& spinlock)
            : m_spinlock{spinlock}
        {
            bool expected;

            // spin until we get the lock
            while (true)
            {
                expected = false;
                if (m_spinlock
                        .compare_exchange_weak(expected, true, std::memory_order_acquire, std::memory_order_relaxed))
                    break;
            }
        }

        /// Release the lock
        ~ScopedSpinlockGuard() { m_spinlock.store(false, std::memory_order_release); }

        ScopedSpinlockGuard(const ScopedSpinlockGuard&) = delete;
        ScopedSpinlockGuard& operator=(const ScopedSpinlockGuard&) & = delete;
        ScopedSpinlockGuard(ScopedSpinlockGuard&&) = delete;
        ScopedSpinlockGuard& operator=(ScopedSpinlockGuard&&) & = delete;
    };
};

/// Macro to declare block allocator support for a class.
/// The block size is automatically determined from sizeof(ClassName).
#define SLANG_RHI_DECLARE_BLOCK_ALLOCATED(ClassName)                                                                   \
public:                                                                                                                \
    void* operator new(size_t count);                                                                                  \
    void operator delete(void* ptr);                                                                                   \
                                                                                                                       \
private:                                                                                                               \
    static ::rhi::RefCountedBlockAllocator<ClassName> s_allocator;

/// Macro to implement block allocator operators in .cpp file.
/// @param ClassName The class name to implement block allocation for.
/// @param BlocksPerPage Number of blocks to allocate per page.
#define SLANG_RHI_IMPLEMENT_BLOCK_ALLOCATED(ClassName, BlocksPerPage)                                                  \
    ::rhi::RefCountedBlockAllocator<ClassName> ClassName::s_allocator(BlocksPerPage);                                  \
    template<typename T>                                                                                               \
    using rhi_RefCountedBlockAllocator = ::rhi::RefCountedBlockAllocator<T>;                                           \
    template<>                                                                                                         \
    int rhi_RefCountedBlockAllocator<ClassName>::s_refcount{0};                                                        \
    template<>                                                                                                         \
    std::atomic<bool> rhi_RefCountedBlockAllocator<ClassName>::s_allocatorLock{false};                                 \
    template<>                                                                                                         \
    ::rhi::BlockAllocator<ClassName>* rhi_RefCountedBlockAllocator<ClassName>::s_allocator{nullptr};                   \
                                                                                                                       \
    void* ClassName::operator new(size_t count)                                                                        \
    {                                                                                                                  \
        return s_allocator.allocate();                                                                                 \
    }                                                                                                                  \
                                                                                                                       \
    void ClassName::operator delete(void* ptr)                                                                         \
    {                                                                                                                  \
        s_allocator.free(static_cast<ClassName*>(ptr));                                                                \
    }

} // namespace rhi
