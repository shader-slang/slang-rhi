#pragma once

#include "common.h"
#include <mutex>

namespace rhi {

/// Block allocator for fixed-size objects.
/// Allocates fixed-size blocks of sizeof(T) out of larger pages.
/// Thread-safe for concurrent allocations and deallocations using a mutex.
///
/// Designed to be safe for use as a static/global variable without relying
/// on static constructors or destructors (avoiding DSO/shared library
/// initialization order hazards). The constructor is constexpr, so static
/// instances are constant-initialized by the compiler. The destructor is
/// intentionally trivial and does NOT free pages. Call releasePages()
/// manually before the allocator goes out of scope to avoid memory leaks.
template<typename T, size_t BlocksPerPage = 256>
class BlockAllocator
{
public:
    constexpr BlockAllocator() = default;
    ~BlockAllocator() = default;

    // Non-copyable, non-movable
    BlockAllocator(const BlockAllocator&) = delete;
    BlockAllocator& operator=(const BlockAllocator&) = delete;
    BlockAllocator(BlockAllocator&&) = delete;
    BlockAllocator& operator=(BlockAllocator&&) = delete;

    /// Release all pages and reset the allocator (NOT thread safe).
    /// Must be called manually to free memory, as the destructor is trivial.
    /// After calling this, the allocator is empty and can be reused.
    void releasePages()
    {
        Page* page = m_pageListHead;
        while (page)
        {
            Page* next = page->next;
            std::free(page);
            page = next;
        }
        m_pageListHead = nullptr;
        m_freeList = nullptr;
        m_numPages = 0;
    }

    /// Allocate a block (thread safe).
    /// @return Pointer to an uninitialized block, or nullptr if allocation fails.
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

    /// Return a block to the free list (thread safe).
    /// The caller is responsible for destroying the object before calling this.
    /// @param ptr Pointer to block to free. No-op if nullptr.
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
            uintptr_t pageEnd = pageStart + BlocksPerPage * sizeof(Block);
            if (addr >= pageStart && addr < pageEnd)
            {
                return true;
            }
            page = page->next;
        }
        return false;
    }

    /// Reset the free list to contain all blocks from all pages (NOT thread safe).
    /// Does not free any pages. Useful for bulk-recycling all blocks without releasing memory.
    void reset()
    {
        FreeBlock* head = nullptr;
        Page* page = m_pageListHead;
        while (page)
        {
            for (size_t i = 0; i < BlocksPerPage; ++i)
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

    /// A block is a union large enough to hold either T or a FreeBlock pointer.
    union Block
    {
        alignas(T) uint8_t data[sizeof(T)];
        FreeBlock freeBlock;
    };

    static_assert(sizeof(Block) >= sizeof(FreeBlock*), "Block must be large enough to hold a pointer");
    static_assert(alignof(Block) >= alignof(T), "Block alignment must be sufficient for T");

    /// A page holds BlocksPerPage blocks and links to the next page.
    /// blocks[1] is a C-style flexible array; actual allocation is BlocksPerPage blocks.
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
        size_t pageSize = sizeof(Page) + (BlocksPerPage - 1) * sizeof(Block);
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
        for (size_t i = 1; i < BlocksPerPage; ++i)
        {
            FreeBlock* block = reinterpret_cast<FreeBlock*>(&page->blocks[i]);
            block->next = m_freeList;
            m_freeList = block;
        }

        // Return the first block
        return reinterpret_cast<T*>(&page->blocks[0]);
    }

    FreeBlock* m_freeList{nullptr};
    mutable std::mutex m_mutex; // Protects all operations
    Page* m_pageListHead{nullptr};
    uint32_t m_numPages{0};
};

/// Macro to declare block allocator support for a class.
/// Overrides operator new/delete to use a static BlockAllocator instance.
#define SLANG_RHI_DECLARE_BLOCK_ALLOCATED(ClassName, BlocksPerPage)                                                    \
public:                                                                                                                \
    using BlockAllocatorType = ::rhi::BlockAllocator<ClassName, BlocksPerPage>;                                        \
    void* operator new(size_t count);                                                                                  \
    void operator delete(void* ptr);                                                                                   \
    static BlockAllocatorType& getAllocator();                                                                         \
                                                                                                                       \
private:                                                                                                               \
    static BlockAllocatorType s_allocator;

/// Macro to implement block allocator operators in a .cpp file.
/// The allocator is constexpr-constructed, requiring no static constructor.
/// releasePages() must be called at shutdown to free memory.
#define SLANG_RHI_IMPLEMENT_BLOCK_ALLOCATED(ClassName)                                                                 \
    SLANG_RHI_STATIC_MUTEX_BEGIN                                                                                       \
    ClassName::BlockAllocatorType ClassName::s_allocator;                                                              \
    SLANG_RHI_STATIC_MUTEX_END                                                                                         \
                                                                                                                       \
    ClassName::BlockAllocatorType& ClassName::getAllocator()                                                           \
    {                                                                                                                  \
        return s_allocator;                                                                                            \
    }                                                                                                                  \
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
