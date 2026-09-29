#pragma once

#include <slang-rhi.h>

#include "core/common.h"
#include "reference.h"
#include "rhi-shared-fwd.h"

#include <mutex>
#include <span>
#include <vector>

namespace rhi {

struct TransientBufferHeapDesc
{
    Size initialPageSize = 64 * 1024;
    Size maxPageSize = 4 * 1024 * 1024;
    Size maxRetainedSize = 4 * 1024 * 1024;
    MemoryType memoryType = MemoryType::Upload;
    BufferUsage usage = BufferUsage::ConstantBuffer;
    ResourceState defaultState = ResourceState::ConstantBuffer;
    Size alignment = 256;
    Size allocationGranularity = 256;
};

/// Queue-owned storage for transient, persistently-mapped buffer data.
///
/// The heap only suballocates coarse chunks. Command-buffer-local arenas perform
/// the per-binding bump allocation so the recording hot path does not take the
/// heap mutex or touch a general-purpose free list.
class TransientBufferHeap
{
public:
    TransientBufferHeap() = default;
    TransientBufferHeap(const TransientBufferHeap&) = delete;
    TransientBufferHeap& operator=(const TransientBufferHeap&) = delete;

    class Page : public RefObject
    {
    public:
        uint64_t getId() const { return m_id; }
        Buffer* getBuffer() const { return m_buffer; }
        Size getCapacity() const { return m_capacity; }
        uint8_t* getMappedData() const { return static_cast<uint8_t*>(m_mappedData); }

    private:
        Page(uint64_t id, Buffer* buffer);

        uint64_t m_id = 0;
        RefPtr<Buffer> m_buffer;
        Size m_capacity = 0;
        Size m_nextOffset = 0;
        Size m_liveSize = 0;
        uint32_t m_liveChunkCount = 0;
        uint64_t m_lastUsedSerial = 0;
        void* m_mappedData = nullptr;

        friend class TransientBufferHeap;
    };

    /// A coarse allocation leased to a command-buffer-local arena.
    /// Chunks are released in batches by TransientBufferArena::reset().
    struct Chunk
    {
        RefPtr<Page> page;
        Offset offset = 0;
        Size size = 0;

        explicit operator bool() const { return page != nullptr; }
        Buffer* getBuffer() const { return page->getBuffer(); }
        uint8_t* getMappedData() const { return page->getMappedData() + offset; }
    };

    void initialize(Device* device, const TransientBufferHeapDesc& desc);
    void release();

    Result allocate(Size size, Chunk* outChunk);
    void free(std::span<Chunk> chunks);

    size_t getNumPages() const;
    Size getCapacity() const;
    Size getUsed() const;
    uint64_t getPageAllocationCount() const;
    Size getInitialPageSize() const { return m_desc.initialPageSize; }
    Size getMaxPageSize() const { return m_desc.maxPageSize; }
    Size getMaxRetainedSize() const { return m_desc.maxRetainedSize; }
    Size getAlignment() const { return m_desc.alignment; }
    Size getAllocationGranularity() const { return m_desc.allocationGranularity; }

    Size alignAllocationSize(Size size) const;

private:
    Device* m_device = nullptr;
    TransientBufferHeapDesc m_desc;
    RefPtr<Page> m_currentPage;
    std::vector<RefPtr<Page>> m_pages;
    uint64_t m_nextPageId = 1;
    uint64_t m_serial = 0;
    uint64_t m_pageAllocationCount = 0;
    Size m_nextPageSize = 0;
    Size m_totalCapacity = 0;
    Size m_totalUsed = 0;
    mutable std::mutex m_mutex;

    Size growPageSize(Size size) const;
    Size getNewPageSize(Size allocationSize) const;
    void updateNextPageSizeLocked();
    Result allocatePageLocked(Size size, Page** outPage);
    void freePageLocked(Page* page);
    void trimRetainedPagesLocked();
};

struct TransientBufferArenaDesc
{
    Size initialChunkSize = 1024;
    Size maxChunkSize = 64 * 1024;
};

/// Command-buffer-local bump allocator backed by coarse TransientBufferHeap chunks.
class TransientBufferArena
{
public:
    TransientBufferArena() = default;
    TransientBufferArena(const TransientBufferArena&) = delete;
    TransientBufferArena& operator=(const TransientBufferArena&) = delete;
    TransientBufferArena(TransientBufferArena&&) = delete;
    TransientBufferArena& operator=(TransientBufferArena&&) = delete;

    struct Allocation
    {
        Buffer* buffer = nullptr;
        Offset offset = 0;
        Size size = 0;
        void* mappedData = nullptr;
    };

    ~TransientBufferArena();

    void initialize(TransientBufferHeap* heap, const TransientBufferArenaDesc& desc = {});
    void reset();

    Result allocate(Size size, Allocation* outAllocation);

    size_t getChunkCount() const { return m_chunks.size(); }
    Size getUsed() const { return m_used; }

private:
    TransientBufferHeap* m_heap = nullptr;
    TransientBufferArenaDesc m_desc;
    std::vector<TransientBufferHeap::Chunk> m_chunks;
    Size m_currentChunkOffset = 0;
    Size m_nextChunkSize = 0;
    Size m_used = 0;
};

} // namespace rhi
