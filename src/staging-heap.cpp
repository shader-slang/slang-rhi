#include "staging-heap.h"

#include "rhi-shared.h"
#include "device.h"

namespace rhi {

const std::thread::id NO_THREAD_ID;

void StagingHeap::initialize(Device* device, Size pageSize, MemoryType memoryType)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_device = device;
    m_pageSize = pageSize;
    m_memoryType = memoryType;

    // Can safely keep pages mapped for all platforms other than WebGPU and Metal.
    // On WebGPU, mapped buffers cannot be used during dispatches.
    // On Metal, this is required to add synchronization between CPU/GPU resource.
    // If this gets any more complex, should init staging heap separately per device
    // with correct configs, but for a single bool that seems overkill.
    m_keepPagesMapped =
        !(device->getInfo().deviceType == DeviceType::WGPU || device->getInfo().deviceType == DeviceType::Metal);
}

void StagingHeap::releaseAllFreePages()
{
    std::vector<Page*> pagesToRemove;
    for (auto& page : m_pages)
    {
        if (page.second->getUsed() == 0)
            pagesToRemove.push_back(page.second.get());
    }
    for (auto page : pagesToRemove)
        freePage(page);
}

void StagingHeap::release()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    releaseAllFreePages();
    SLANG_RHI_ASSERT(m_totalUsed == 0);
    SLANG_RHI_ASSERT(m_pages.size() == 0);
    m_pages.clear();
}

Result StagingHeap::allocHandle(size_t size, MetaData metadata, StagingHeap::Handle** outHandle)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return allocHandleInternal(size, metadata, outHandle);
}

Result StagingHeap::alloc(size_t size, MetaData metadata, StagingHeap::Allocation* outAllocation)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return allocInternal(size, metadata, outAllocation);
}

Result StagingHeap::allocHandleInternal(size_t size, MetaData metadata, StagingHeap::Handle** outHandle)
{
    *outHandle = nullptr;
    Allocation allocation;
    SLANG_RETURN_ON_FAIL(allocInternal(size, metadata, &allocation));

    RefPtr<Handle> res = new Handle(this, allocation);
    returnRefPtr(outHandle, res);
    return SLANG_OK;
}

Result StagingHeap::allocInternal(size_t size, MetaData metadata, StagingHeap::Allocation* outAllocation)
{
    // Get aligned size.
    size_t alignedSize = alignUp(size);

    // If pages are kept mapped, then can't have multiple threads allocating from the same page,
    // so record the thread id to lock pages to.
    auto thread_id = m_keepPagesMapped ? NO_THREAD_ID : std::this_thread::get_id();

    // Attempt to allocate from page if size is less than page size.
    if (alignedSize < m_pageSize)
    {
        for (auto& page_pair : m_pages)
        {
            Page* page = page_pair.second;
            if (page->getLockedToThread() == NO_THREAD_ID || page->getLockedToThread() == thread_id)
            {
                std::list<Node>::iterator node;
                if (page->allocNode(alignedSize, metadata, thread_id, node))
                {
                    Allocation res;
                    res.page = page;
                    res.node = node;
                    m_totalUsed += alignedSize;
                    *outAllocation = res;
                    return SLANG_OK;
                }
            }
        }
    }

    // Can't fit in existing page, so allocate from new one
    size_t pageSize = alignedSize < m_pageSize ? m_pageSize : alignedSize;
    Page* page;
    SLANG_RETURN_ON_FAIL(allocPage(pageSize, &page));
    std::list<Node>::iterator node;
    page->allocNode(alignedSize, metadata, thread_id, node);

    Allocation res;
    res.page = page;
    res.node = node;
    m_totalUsed += alignedSize;

    *outAllocation = res;
    return SLANG_OK;
}

Result StagingHeap::stageHandle(const void* data, size_t size, MetaData metadata, Handle** outHandle)
{
    // Perform thread safe allocation.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        SLANG_RETURN_ON_FAIL(allocHandleInternal(size, metadata, outHandle));
    }

    // Copy data to page.
    void* buffer;
    SLANG_RETURN_ON_FAIL(map((*outHandle)->getAllocation(), &buffer));
    memcpy(buffer, data, size);
    unmap((*outHandle)->getAllocation());
    return SLANG_OK;
}

Result StagingHeap::stage(const void* data, size_t size, MetaData metadata, Allocation* outAllocation)
{
    // Perform thread safe allocation.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        SLANG_RETURN_ON_FAIL(allocInternal(size, metadata, outAllocation));
    }

    // Copy data to page.
    void* buffer;
    SLANG_RETURN_ON_FAIL(map(*outAllocation, &buffer));
    memcpy(buffer, data, size);
    unmap(*outAllocation);
    return SLANG_OK;
}

Result StagingHeap::map(const Allocation& allocation, void** outAddress)
{
    Page* page = allocation.getPage();
    Offset offset = allocation.getOffset();
    if (!m_keepPagesMapped)
        SLANG_RETURN_ON_FAIL(page->map(m_device));
    *outAddress = page->getMapped() + offset;
    return SLANG_OK;
}

Result StagingHeap::unmap(const Allocation& allocation)
{
    if (!m_keepPagesMapped)
        return allocation.getPage()->unmap(m_device);
    else
        return SLANG_OK;
}

void StagingHeap::free(Allocation allocation)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Decrement overall total used (before freeing node).
    m_totalUsed -= allocation.node->size;

    // Free the node from the page.
    RefPtr<Page> page = m_pages[allocation.getPageId()];
    page->freeNode(allocation.node);

    // Free page if now have more than 1 empty page or this is non-standard page size.
    if (page->getUsed() == 0)
    {
        if (page->getCapacity() == m_pageSize)
        {
            int empty_pages = 0;
            for (auto& p : m_pages)
                if (p.second->getUsed() == 0)
                    empty_pages++;
            if (empty_pages > 1)
                freePage(page);
        }
        else
        {
            freePage(page);
        }
    }
}

Result StagingHeap::allocPage(size_t size, StagingHeap::Page** outPage)
{
    *outPage = nullptr;

    ComPtr<IBuffer> bufferPtr;
    BufferDesc bufferDesc;
    bufferDesc.usage = BufferUsage::CopyDestination | BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::General;
    bufferDesc.memoryType = m_memoryType;
    bufferDesc.size = size;

    // Attempt to create buffer.
    SLANG_RETURN_ON_FAIL(m_device->createBuffer(bufferDesc, nullptr, bufferPtr.writeRef()));

    // Create page and store buffer pointer.
    StagingHeap::Page* page = new Page(m_nextPageId++, checked_cast<Buffer*>(bufferPtr.get()));
    m_pages.insert({page->getId(), page});
    m_totalCapacity += size;

    // Break references to device as buffer is owned by heap, which is owned by device.
    page->getBuffer()->breakStrongReferenceToDevice();

    // If always mapped, map page now
    if (m_keepPagesMapped)
        SLANG_RETURN_ON_FAIL(page->map(m_device));

    *outPage = page;
    return SLANG_OK;
}

void StagingHeap::freePage(StagingHeap::Page* page)
{
    SLANG_RHI_ASSERT(page->getUsed() == 0);
    m_totalCapacity -= page->getCapacity();

    // If always mapped, unmap page now
    if (m_keepPagesMapped)
        page->unmap(m_device);

    m_pages.erase(page->getId());
}

void StagingHeap::checkConsistency()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t totalUsed = 0;
    for (auto& page : m_pages)
    {
        page.second->checkConsistency();
        totalUsed += page.second->getUsed();
    }
    SLANG_RHI_ASSERT(totalUsed == m_totalUsed);
}

StagingHeap::Page::Page(int id, RefPtr<Buffer> buffer)
    : m_id(id)
    , m_buffer(buffer)
{
    m_totalCapacity = buffer->getDesc().size;
    m_totalUsed = 0;
    m_nodes.push_back({0, m_totalCapacity, true, {}});
};

bool StagingHeap::Page::allocNode(
    Size size,
    StagingHeap::MetaData metadta,
    std::thread::id lock_to_thread,
    std::list<Node>::iterator& res
)
{
    // Check if page is locked to a thread, and if so, that it is the same thread.
    SLANG_RHI_ASSERT(m_locked_to_thread == lock_to_thread || m_locked_to_thread == NO_THREAD_ID);

    // Scan nodes for a free slot greater than or equal to size requested
    for (auto node = m_nodes.begin(); node != m_nodes.end(); ++node)
    {
        if (node->free && node->size >= size)
        {
            // Got one. Increment total used in page.
            m_totalUsed += size;

            // If node is bigger than necessary, split it.
            if (node->size > size)
            {
                auto next = std::next(node);
                m_nodes.insert(next, {node->offset + size, node->size - size, true, {}});
                node->size = size;
            }

            // Mark node as not free, and store meta data.
            node->free = false;
            node->metadata = metadta;

            // Lock to the thread (if specified)
            m_locked_to_thread = lock_to_thread;

            // Return iterator to node.
            res = node;
            return true;
        }
    }

    // No free node found.
    res = m_nodes.end();
    return false;
}

void StagingHeap::Page::freeNode(std::list<StagingHeap::Node>::iterator node)
{
    SLANG_RHI_ASSERT(!node->free);

    // Decrement total used in page.
    m_totalUsed -= node->size;

    // Merge with previous node if it exists and is free.
    if (node != m_nodes.begin())
    {
        auto prev = std::prev(node);
        if (prev->free)
        {
            prev->size += node->size;
            m_nodes.erase(node);
            node = prev;
        }
    }

    // Merge with next node if it exists and is free.
    auto next = std::next(node);
    if (next != m_nodes.end())
    {
        if (next->free)
        {
            node->size += next->size;
            m_nodes.erase(next);
        }
    }

    // Mark node as free.
    node->free = true;

    // Unlock thread if back to 0 allocs
    if (m_totalUsed == 0)
        m_locked_to_thread = NO_THREAD_ID;
}

void StagingHeap::Page::checkConsistency()
{
    size_t totalUsed = 0;
    size_t offset = 0;
    bool prevFree = false;
    for (const StagingHeap::Node& node : m_nodes)
    {
        // Check node offset matches the tracked offset.
        SLANG_RHI_ASSERT(node.offset == offset);

        // Track total allocated.
        if (!node.free)
        {
            totalUsed += node.size;
        }

        // Check for free node following a free node.
        if (offset != 0 && node.free)
        {
            SLANG_RHI_ASSERT(!prevFree);
        }

        // Track offset + free state.
        offset += node.size;
        prevFree = node.free;
    }

    // Check total used matches tracked total used.
    SLANG_RHI_ASSERT(totalUsed == m_totalUsed);

    // Check total capacity matches tracked total capacity.
    SLANG_RHI_ASSERT(offset == m_totalCapacity);
}

Result StagingHeap::Page::map(Device* device)
{
    SLANG_RHI_ASSERT(!m_mapped);
    return device->mapBuffer(
        m_buffer,
        m_buffer->m_desc.memoryType == MemoryType::Upload ? CpuAccessMode::Write : CpuAccessMode::Read,
        &m_mapped
    );
}

Result StagingHeap::Page::unmap(Device* device)
{
    SLANG_RHI_ASSERT(m_mapped);
    SLANG_RETURN_ON_FAIL(device->unmapBuffer(m_buffer));
    m_mapped = nullptr;
    return SLANG_OK;
}

} // namespace rhi
