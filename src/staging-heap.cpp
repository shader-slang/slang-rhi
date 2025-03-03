#include "staging-heap.h"

#include "rhi-shared.h"
#include "device.h"

namespace rhi {

void StagingHeap::initialize(Device* device)
{
    m_device = device;
}

RefPtr<StagingHeap::Handle> StagingHeap::allocHandle(size_t size, MetaData metadata)
{
    Allocation allocation = alloc(size, metadata);
    return new Handle(this, allocation);
}

StagingHeap::Allocation StagingHeap::alloc(size_t size, MetaData metadata)
{
    // Get aligned size.
    size_t aligned_size = alignUp(size);

    // Attempt to allocate from page if size is less than page size.
    if (aligned_size < m_page_size)
    {
        for (auto& page : m_pages)
        {
            std::list<Node>::iterator node;
            if (page.second->allocNode(aligned_size, metadata, node))
            {
                Allocation res;
                res.buffer = page.second->getBuffer();
                res.node = node;
                m_total_used += aligned_size;
                return res;
            }
        }
    }

    // Can't fit in existing page, so allocate from new one
    size_t page_size = aligned_size < m_page_size ? m_page_size : aligned_size;
    RefPtr<Page> page = allocPage(page_size);
    std::list<Node>::iterator node;
    page->allocNode(aligned_size, metadata, node);

    Allocation res;
    res.buffer = page->getBuffer();
    res.node = node;
    m_total_used += aligned_size;

    return res;
}

void StagingHeap::free(Allocation allocation)
{
    // Decrement overall total used (before freeing node).
    m_total_used -= allocation.node->size;

    // Free the node from the page.
    RefPtr<Page> page = m_pages[allocation.getPageId()];
    page->freeNode(allocation.node);

    // Free page if now have more than 1 empty page or this is none-standard page size.
    if (page->getUsed() == 0)
    {
        if (page->getCapacity() == m_page_size)
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
    bufferDesc.usage = BufferUsage::CopyDestination | BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::General;
    bufferDesc.memoryType = MemoryType::Upload;
    bufferDesc.size = size;

    // TODO(staging-heap): Check for failure
    m_device->createBuffer(bufferDesc, nullptr, bufferPtr.writeRef());

    // Create page and store buffer pointer
    RefPtr<Page> page = new Page(m_next_page_id++, checked_cast<Buffer*>(bufferPtr.get()));
    m_pages.insert({page->getId(), page});
    m_total_capacity += size;

    // TODO(staging-heap): Check wtf this is
    //  The buffer is owned by the page.
    page->getBuffer()->comFree();

    return page;
}

void StagingHeap::checkConsistency()
{
    size_t total_used = 0;
    for (auto& page : m_pages)
    {
        page.second->checkConsistency();
        total_used += page.second->getUsed();
    }
    SLANG_RHI_ASSERT(total_used == m_total_used);
}

StagingHeap::Page::Page(int id, RefPtr<Buffer> buffer)
    : m_id(id)
    , m_buffer(buffer)
{
    m_total_capacity = buffer->getDesc().size;
    m_total_used = 0;
    m_nodes.push_back({0, m_total_capacity, true, m_id, {}});
};

bool StagingHeap::Page::allocNode(Size size, StagingHeap::MetaData metadta, std::list<Node>::iterator& res)
{
    for (auto node = m_nodes.begin(); node != m_nodes.end(); ++node)
    {
        if (node->free && node->size >= size)
        {
            m_total_used += size;
            if (node->size > size)
            {
                auto next = std::next(node);
                m_nodes.insert(next, {node->offset + size, node->size - size, true, m_id, {}});
                node->size = size;
            }
            node->free = false;
            node->metadata = metadta;
            res = node;
            return true;
        }
    }
    res = m_nodes.end();
    return false;
}

void StagingHeap::Page::freeNode(std::list<StagingHeap::Node>::iterator node)
{
    SLANG_RHI_ASSERT(!node->free);
    SLANG_RHI_ASSERT(node->pageid == m_id);

    m_total_used -= node->size;

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

    auto next = std::next(node);
    if (next != m_nodes.end())
    {
        if (next->free)
        {
            node->size += next->size;
            m_nodes.erase(next);
        }
    }

    node->free = true;
}

void StagingHeap::Page::checkConsistency()
{
    size_t total_used = 0;
    size_t offset = 0;
    bool prev_free = false;
    for (StagingHeap::Node& node : m_nodes)
    {
        SLANG_RHI_ASSERT(node.pageid == m_id);
        SLANG_RHI_ASSERT(node.offset == offset);
        if (!node.free)
        {
            total_used += node.size;
        }
        if (offset != 0 && node.free)
        {
            SLANG_RHI_ASSERT(!prev_free);
        }
        offset += node.size;
        prev_free = node.free;
    }
    SLANG_RHI_ASSERT(total_used == m_total_used);
    SLANG_RHI_ASSERT(offset == m_total_capacity);
}

} // namespace rhi
