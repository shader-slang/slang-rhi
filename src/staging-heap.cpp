#include "staging-heap.h"

#include "rhi-shared.h"
#include "device.h"

namespace rhi {

void StagingHeap::initialize(Device* device)
{
    m_device = device;
}

void StagingHeap::releaseAllFreePages()
{
    std::vector<int> pagesToRemove;
    for (auto& page : m_pages)
    {
        if (page.second->getUsed() == 0)
            pagesToRemove.push_back(page.first);
    }
    for (auto page_id : pagesToRemove)
        m_pages.erase(page_id);
}

void StagingHeap::release()
{
    releaseAllFreePages();
    SLANG_RHI_ASSERT(m_totalUsed == 0);
    SLANG_RHI_ASSERT(m_pages.size() == 0);
    m_pages.clear();
}

RefPtr<StagingHeap::Handle> StagingHeap::allocHandle(size_t size, MetaData metadata)
{
    Allocation allocation = alloc(size, metadata);
    return new Handle(this, allocation);
}

StagingHeap::Allocation StagingHeap::alloc(size_t size, MetaData metadata)
{
    // Get aligned size.
    size_t alignedSize = alignUp(size);

    // Attempt to allocate from page if size is less than page size.
    if (alignedSize < m_pageSize)
    {
        for (auto& page : m_pages)
        {
            std::list<Node>::iterator node;
            if (page.second->allocNode(alignedSize, metadata, node))
            {
                Allocation res;
                res.buffer = page.second->getBuffer();
                res.node = node;
                m_totalUsed += alignedSize;
                return res;
            }
        }
    }

    // Can't fit in existing page, so allocate from new one
    size_t pageSize = alignedSize < m_pageSize ? m_pageSize : alignedSize;
    RefPtr<Page> page = allocPage(pageSize);
    std::list<Node>::iterator node;
    page->allocNode(alignedSize, metadata, node);

    Allocation res;
    res.buffer = page->getBuffer();
    res.node = node;
    m_totalUsed += alignedSize;

    return res;
}

void StagingHeap::free(Allocation allocation)
{
    // Decrement overall total used (before freeing node).
    m_totalUsed -= allocation.node->size;

    // Free the node from the page.
    RefPtr<Page> page = m_pages[allocation.getPageId()];
    page->freeNode(allocation.node);

    // Free page if now have more than 1 empty page or this is none-standard page size.
    if (page->getUsed() == 0)
    {
        if (page->getCapacity() == m_pageSize)
        {
            int empty_pages = 0;
            for (auto& p : m_pages)
                if (p.second->getUsed() == 0)
                    empty_pages++;
            if (empty_pages > 1)
                m_pages.erase(page->getId());
        }
        else
        {
            m_pages.erase(page->getId());
        }
    }
}

RefPtr<StagingHeap::Page> StagingHeap::allocPage(size_t size)
{
    ComPtr<IBuffer> bufferPtr;
    BufferDesc bufferDesc;
    bufferDesc.usage = BufferUsage::CopyDestination | BufferUsage::CopySource | BufferUsage::ConstantBuffer;
    bufferDesc.defaultState = ResourceState::General;
    bufferDesc.memoryType = MemoryType::Upload;
    bufferDesc.size = size;

    // TODO(staging-heap): Check for failure
    m_device->createBuffer(bufferDesc, nullptr, bufferPtr.writeRef());

    // Create page and store buffer pointer
    RefPtr<Page> page = new Page(m_nextPageId++, checked_cast<Buffer*>(bufferPtr.get()));
    m_pages.insert({page->getId(), page});
    m_totalCapacity += size;

    // TODO(staging-heap): Find out what this is
    //  The buffer is owned by the page.
    page->getBuffer()->comFree();

    return page;
}

void StagingHeap::checkConsistency()
{
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
    m_nodes.push_back({0, m_totalCapacity, true, m_id, {}});
};

bool StagingHeap::Page::allocNode(Size size, StagingHeap::MetaData metadta, std::list<Node>::iterator& res)
{
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
                m_nodes.insert(next, {node->offset + size, node->size - size, true, m_id, {}});
                node->size = size;
            }

            // Mark node as not free, and store meta data.
            node->free = false;
            node->metadata = metadta;

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
    SLANG_RHI_ASSERT(node->pageid == m_id);

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
}

void StagingHeap::Page::checkConsistency()
{
    size_t totalUsed = 0;
    size_t offset = 0;
    bool prevFree = false;
    for (StagingHeap::Node& node : m_nodes)
    {
        // Check node page is correct.
        SLANG_RHI_ASSERT(node.pageid == m_id);

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

} // namespace rhi
