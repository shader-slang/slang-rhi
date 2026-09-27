#include "transient-buffer-heap.h"

#include "device.h"
#include "rhi-shared.h"

#include <algorithm>

namespace rhi {

TransientBufferHeap::Page::Page(uint64_t id, Buffer* buffer)
    : m_id(id)
    , m_buffer(buffer)
    , m_capacity(buffer->getDesc().size)
{
}

void TransientBufferHeap::initialize(Device* device, const TransientBufferHeapDesc& desc)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    SLANG_RHI_ASSERT(device);
    SLANG_RHI_ASSERT(desc.initialPageSize > 0);
    SLANG_RHI_ASSERT(desc.maxPageSize >= desc.initialPageSize);
    SLANG_RHI_ASSERT(desc.maxRetainedSize == 0 || desc.maxRetainedSize >= desc.initialPageSize);
    SLANG_RHI_ASSERT(desc.alignment > 0);
    SLANG_RHI_ASSERT(desc.allocationGranularity > 0);
    SLANG_RHI_ASSERT(m_pages.empty());
    SLANG_RHI_ASSERT(m_totalUsed == 0);

    m_device = device;
    m_desc = desc;
    m_nextPageSize = desc.initialPageSize;
}

void TransientBufferHeap::release()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    SLANG_RHI_ASSERT(m_totalUsed == 0);
    if (m_totalUsed != 0)
        return;

    m_currentPage = nullptr;
    while (!m_pages.empty())
        freePageLocked(m_pages.back());
}

Size TransientBufferHeap::alignAllocationSize(Size size) const
{
    const Size granularity = m_desc.allocationGranularity;
    return (size + granularity - 1) / granularity * granularity;
}

Size TransientBufferHeap::growPageSize(Size size) const
{
    if (size >= m_desc.maxPageSize)
        return m_desc.maxPageSize;
    return size > m_desc.maxPageSize / 2 ? m_desc.maxPageSize : size * 2;
}

Size TransientBufferHeap::getNewPageSize(Size allocationSize) const
{
    Size pageSize = m_nextPageSize;
    while (pageSize < allocationSize && pageSize < m_desc.maxPageSize)
        pageSize = growPageSize(pageSize);
    return pageSize < allocationSize ? allocationSize : pageSize;
}

void TransientBufferHeap::updateNextPageSizeLocked()
{
    m_nextPageSize = m_desc.initialPageSize;
    for (const auto& page : m_pages)
    {
        const Size pageSize = page->m_capacity;
        if (pageSize >= m_desc.initialPageSize && pageSize <= m_desc.maxPageSize)
            m_nextPageSize = max(m_nextPageSize, growPageSize(pageSize));
    }
}

Result TransientBufferHeap::allocatePageLocked(Size size, Page** outPage)
{
    *outPage = nullptr;

    BufferDesc desc = {};
    desc.size = size;
    desc.memoryType = m_desc.memoryType;
    desc.usage = m_desc.usage;
    desc.defaultState = m_desc.defaultState;

    ComPtr<IBuffer> buffer;
    SLANG_RETURN_ON_FAIL(m_device->createBuffer(desc, nullptr, buffer.writeRef()));

    RefPtr<Page> page = new Page(m_nextPageId, checked_cast<Buffer*>(buffer.get()));
    SLANG_RETURN_ON_FAIL(m_device->mapBuffer(page->m_buffer, CpuAccessMode::Write, &page->m_mappedData));
    if (!page->m_mappedData)
        return SLANG_FAIL;

    // The queue-owned heap controls the buffer lifetime and the queue itself is device-owned.
    page->m_buffer->breakStrongReferenceToDevice();

    m_nextPageId++;
    m_pageAllocationCount++;
    m_totalCapacity += size;
    m_pages.push_back(page);
    *outPage = page;
    return SLANG_OK;
}

Result TransientBufferHeap::allocate(Size size, Chunk* outChunk)
{
    if (!outChunk)
        return SLANG_E_INVALID_ARG;
    *outChunk = {};
    if (size == 0)
        return SLANG_E_INVALID_ARG;

    const Size alignedSize = alignAllocationSize(size);
    if (alignedSize > m_desc.maxPageSize)
        return SLANG_FAIL;
    std::lock_guard<std::mutex> lock(m_mutex);

    auto tryAllocate = [&](Page* page) -> bool
    {
        if (!page)
            return false;
        const Offset offset = math::calcAligned(page->m_nextOffset, m_desc.alignment);
        if (offset > page->m_capacity || alignedSize > page->m_capacity - offset)
            return false;

        page->m_nextOffset = offset + alignedSize;
        page->m_liveSize += alignedSize;
        page->m_liveChunkCount++;
        page->m_lastUsedSerial = ++m_serial;
        m_totalUsed += alignedSize;
        outChunk->page = page;
        outChunk->offset = offset;
        outChunk->size = alignedSize;
        return true;
    };

    if (tryAllocate(m_currentPage))
        return SLANG_OK;

    // Reuse the smallest retained empty page that can satisfy the chunk.
    Page* page = nullptr;
    for (const auto& candidate : m_pages)
    {
        if (candidate->m_liveChunkCount != 0 || candidate->m_capacity < alignedSize)
            continue;
        if (!page || candidate->m_capacity < page->m_capacity)
            page = candidate;
    }

    if (!page)
    {
        const Size pageSize = getNewPageSize(alignedSize);
        SLANG_RETURN_ON_FAIL(allocatePageLocked(pageSize, &page));
        if (pageSize <= m_desc.maxPageSize)
            m_nextPageSize = growPageSize(pageSize);
    }

    m_currentPage = page;
    return tryAllocate(page) ? SLANG_OK : SLANG_FAIL;
}

void TransientBufferHeap::free(std::span<Chunk> chunks)
{
    if (chunks.empty())
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    for (Chunk& chunk : chunks)
    {
        if (!chunk)
            continue;

        Page* page = chunk.page;
        SLANG_RHI_ASSERT(page->m_liveChunkCount > 0);
        SLANG_RHI_ASSERT(page->m_liveSize >= chunk.size);
        SLANG_RHI_ASSERT(m_totalUsed >= chunk.size);
        page->m_liveChunkCount--;
        page->m_liveSize -= chunk.size;
        m_totalUsed -= chunk.size;

        if (page->m_liveChunkCount == 0)
        {
            SLANG_RHI_ASSERT(page->m_liveSize == 0);
            page->m_nextOffset = 0;
            page->m_lastUsedSerial = ++m_serial;
        }

        chunk = {};
    }

    trimRetainedPagesLocked();
    updateNextPageSizeLocked();
}

void TransientBufferHeap::trimRetainedPagesLocked()
{
    Size retainedSize = 0;
    for (const auto& page : m_pages)
    {
        if (page->m_liveChunkCount == 0)
            retainedSize += page->m_capacity;
    }

    while (retainedSize > m_desc.maxRetainedSize)
    {
        Page* candidate = nullptr;
        for (const auto& page : m_pages)
        {
            if (page->m_liveChunkCount != 0)
                continue;

            // Pages that cannot fit in the retention budget are never useful warm pages.
            if (page->m_capacity > m_desc.maxRetainedSize)
            {
                candidate = page;
                break;
            }
            if (!candidate || page->m_lastUsedSerial < candidate->m_lastUsedSerial)
                candidate = page;
        }
        if (!candidate)
            break;

        retainedSize -= candidate->m_capacity;
        freePageLocked(candidate);
    }
}

void TransientBufferHeap::freePageLocked(Page* page)
{
    SLANG_RHI_ASSERT(page);
    SLANG_RHI_ASSERT(page->m_liveChunkCount == 0);
    SLANG_RHI_ASSERT(page->m_liveSize == 0);

    if (m_currentPage.get() == page)
        m_currentPage = nullptr;
    if (page->m_mappedData)
    {
        m_device->unmapBuffer(page->m_buffer);
        page->m_mappedData = nullptr;
    }
    m_totalCapacity -= page->m_capacity;
    std::erase_if(
        m_pages,
        [page](const RefPtr<Page>& value)
        {
            return value.get() == page;
        }
    );
}

size_t TransientBufferHeap::getNumPages() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pages.size();
}

Size TransientBufferHeap::getCapacity() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_totalCapacity;
}

Size TransientBufferHeap::getUsed() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_totalUsed;
}

uint64_t TransientBufferHeap::getPageAllocationCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pageAllocationCount;
}

TransientBufferArena::~TransientBufferArena()
{
    reset();
}

void TransientBufferArena::initialize(TransientBufferHeap* heap, const TransientBufferArenaDesc& desc)
{
    SLANG_RHI_ASSERT(heap);
    SLANG_RHI_ASSERT(desc.initialChunkSize > 0);
    SLANG_RHI_ASSERT(desc.maxChunkSize >= desc.initialChunkSize);
    SLANG_RHI_ASSERT(desc.maxChunkSize <= heap->getMaxPageSize());
    SLANG_RHI_ASSERT(m_chunks.empty());

    m_heap = heap;
    m_desc = desc;
    m_nextChunkSize = desc.initialChunkSize;
}

void TransientBufferArena::reset()
{
    if (m_heap)
        m_heap->free(m_chunks);
    m_chunks.clear();
    m_currentChunkOffset = 0;
    m_nextChunkSize = m_desc.initialChunkSize;
    m_used = 0;
}

Result TransientBufferArena::allocate(Size size, Allocation* outAllocation)
{
    if (!outAllocation)
        return SLANG_E_INVALID_ARG;
    *outAllocation = {};
    if (size == 0)
        return SLANG_E_INVALID_ARG;
    if (!m_heap || size > m_heap->getMaxPageSize())
        return SLANG_FAIL;

    const Size alignedSize = m_heap->alignAllocationSize(size);
    if (alignedSize > m_heap->getMaxPageSize())
        return SLANG_FAIL;

    Size allocationOffset = m_chunks.empty() ? 0 : math::calcAligned(m_currentChunkOffset, m_heap->getAlignment());
    if (m_chunks.empty() || allocationOffset > m_chunks.back().size ||
        alignedSize > m_chunks.back().size - allocationOffset)
    {
        const Size chunkSize = max(alignedSize, m_nextChunkSize);
        TransientBufferHeap::Chunk chunk;
        SLANG_RETURN_ON_FAIL(m_heap->allocate(chunkSize, &chunk));
        m_chunks.push_back(std::move(chunk));
        m_currentChunkOffset = 0;
        allocationOffset = 0;

        if (chunkSize <= m_desc.maxChunkSize)
        {
            const Size grownChunkSize = chunkSize > m_desc.maxChunkSize / 2 ? m_desc.maxChunkSize : chunkSize * 2;
            m_nextChunkSize = max(m_nextChunkSize, grownChunkSize);
        }
    }

    const TransientBufferHeap::Chunk& chunk = m_chunks.back();
    SLANG_RHI_ASSERT(allocationOffset + alignedSize <= chunk.size);
    outAllocation->buffer = chunk.getBuffer();
    outAllocation->offset = chunk.offset + allocationOffset;
    outAllocation->size = alignedSize;
    outAllocation->mappedData = chunk.getMappedData() + allocationOffset;
    m_currentChunkOffset = allocationOffset + alignedSize;
    m_used += alignedSize;
    return SLANG_OK;
}

} // namespace rhi
