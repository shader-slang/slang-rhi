#include "persistent-buffer-pool.h"
#include "device.h"
#include "rhi-shared.h"

#include <limits>

namespace rhi {

struct PersistentBufferPool::Page
{
    Page(Size capacity, Size alignment, bool dedicated)
    {
        // OffsetAllocator reserves a free-list sentinel and a node for the initial free region.
        if (!dedicated)
            regions =
                std::make_unique<OffsetAllocator>(uint32_t(capacity / alignment), uint32_t(capacity / alignment) + 2);
    }
    RefPtr<Buffer> buffer;
    void* mappedData = nullptr;
    std::unique_ptr<OffsetAllocator> regions;
    uint32_t liveAllocations = 0;
};

PersistentBufferPool::PersistentBufferPool() = default;
PersistentBufferPool::~PersistentBufferPool()
{
    // Native allocations must be released before the backend destroys its device/queue.
    SLANG_RHI_ASSERT(m_pages.empty());
}

PersistentBufferPool::Allocation::Allocation(
    PersistentBufferPool* pool,
    Page* page,
    OffsetAllocator::Allocation region,
    Size size
)
    : m_device(pool->m_device)
    , m_pool(pool)
    , m_page(page)
    , m_region(region)
    , m_size(size)
{
}

PersistentBufferPool::Allocation::~Allocation()
{
    m_pool->free(this);
}
Buffer* PersistentBufferPool::Allocation::getBuffer() const
{
    return m_page->buffer;
}
Offset PersistentBufferPool::Allocation::getOffset() const
{
    return Size(m_region.offset) * m_pool->m_alignment;
}
void* PersistentBufferPool::Allocation::getMappedData() const
{
    return static_cast<uint8_t*>(m_page->mappedData) + getOffset();
}

void PersistentBufferPool::initialize(Device* device, Size alignment, Size pageSize, Size maxRetainedSize)
{
    SLANG_RHI_ASSERT(device && alignment && pageSize && pageSize % alignment == 0);
    SLANG_RHI_ASSERT((pageSize & (pageSize - 1)) == 0);
    SLANG_RHI_ASSERT(pageSize / alignment <= UINT32_MAX - 2);
    SLANG_RHI_ASSERT(!m_device);
    m_device = device;
    m_alignment = alignment;
    m_pageSize = pageSize;
    m_maxRetainedSize = maxRetainedSize;
}

Result PersistentBufferPool::createPage(Size capacity, Page*& outPage)
{
    outPage = nullptr;
    BufferDesc desc = {};
    desc.size = capacity;
    desc.memoryType = MemoryType::Upload;
    desc.usage = BufferUsage::ConstantBuffer | BufferUsage::CopyDestination;
    desc.defaultState = ResourceState::ConstantBuffer;
    ComPtr<IBuffer> buffer;
    SLANG_RETURN_ON_FAIL(m_device->createBuffer(desc, nullptr, buffer.writeRef()));
    auto page = std::make_unique<Page>(capacity, m_alignment, capacity > m_pageSize);
    page->buffer = checked_cast<Buffer*>(buffer.get());
    SLANG_RETURN_ON_FAIL(m_device->mapBuffer(buffer, CpuAccessMode::Write, &page->mappedData));
    if (!page->mappedData)
        return SLANG_FAIL;
    page->buffer->breakStrongReferenceToDevice();
    outPage = page.get();
    m_pages.push_back(std::move(page));
    m_stats.pageCount++;
    m_stats.pageAllocationCount++;
    m_stats.capacity += capacity;
    m_emptyCapacity += capacity;
    return SLANG_OK;
}

Result PersistentBufferPool::allocate(Size size, RefPtr<Allocation>& outAllocation)
{
    outAllocation = nullptr;
    if (!m_device || !size || size > std::numeric_limits<Size>::max() - (m_alignment - 1))
        return SLANG_E_INVALID_ARG;
    const Size units = (size + m_alignment - 1) / m_alignment;
    if (units >= uint64_t(UINT32_MAX))
        return SLANG_E_OUT_OF_MEMORY;
    const Size alignedSize = units * m_alignment;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto tryAllocate = [&](Page* page) -> bool
    {
        if (!page)
            return false;
        OffsetAllocator::Allocation region;
        if (page->regions)
        {
            if (page->regions->getFreeStorage() < units)
                return false;
            region = page->regions->allocate(uint32_t(units));
            if (!region)
                return false;
        }
        else
        {
            if (page->liveAllocations || page->buffer->m_desc.size < alignedSize)
                return false;
            region.offset = 0;
        }
        if (!page->liveAllocations++)
            m_emptyCapacity -= page->buffer->m_desc.size;
        m_stats.used += alignedSize;
        m_stats.allocationCount++;
        outAllocation = new Allocation(this, page, region, alignedSize);
        m_currentPage = page;
        return true;
    };
    if (tryAllocate(m_currentPage))
        return SLANG_OK;
    for (const auto& page : m_pages)
    {
        if (page.get() != m_currentPage && tryAllocate(page.get()))
            return SLANG_OK;
    }
    Page* page;
    SLANG_RETURN_ON_FAIL(createPage(max(m_pageSize, alignedSize), page));
    if (tryAllocate(page))
        return SLANG_OK;
    destroyPage(page);
    return SLANG_E_OUT_OF_MEMORY;
}

void PersistentBufferPool::free(Allocation* allocation)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto page = allocation->m_page;
    if (page->regions)
        page->regions->free(allocation->m_region);
    SLANG_RHI_ASSERT(page->liveAllocations);
    const bool empty = --page->liveAllocations == 0;
    if (empty)
        m_emptyCapacity += page->buffer->m_desc.size;
    m_stats.used -= allocation->m_size;
    m_stats.allocationCount--;
    if (empty && (m_emptyCapacity > m_maxRetainedSize || page->buffer->m_desc.size > m_pageSize))
        trimEmptyPages();
}

void PersistentBufferPool::destroyPage(Page* page)
{
    SLANG_RHI_ASSERT(!page->liveAllocations);
    if (m_currentPage == page)
        m_currentPage = nullptr;
    m_device->unmapBuffer(page->buffer);
    m_stats.capacity -= page->buffer->m_desc.size;
    m_emptyCapacity -= page->buffer->m_desc.size;
    m_stats.pageCount--;
    std::erase_if(
        m_pages,
        [&](const auto& candidate)
        {
            return candidate.get() == page;
        }
    );
}

void PersistentBufferPool::trimEmptyPages()
{
    for (size_t i = 0; i < m_pages.size();)
    {
        auto page = m_pages[i].get();
        if (!page->liveAllocations && (m_emptyCapacity > m_maxRetainedSize || page->buffer->m_desc.size > m_pageSize))
            destroyPage(page);
        else
            ++i;
    }
}

void PersistentBufferPool::release()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    SLANG_RHI_ASSERT(m_stats.allocationCount == 0);
    if (m_stats.allocationCount)
        return;
    while (!m_pages.empty())
        destroyPage(m_pages.back().get());
}

PersistentBufferPool::Stats PersistentBufferPool::getStats() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

} // namespace rhi
