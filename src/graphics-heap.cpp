#include "graphics-heap.h"

#include "rhi-shared.h"

namespace rhi {

Result GraphicsHeap::allocate(const GraphicsAllocDesc& desc, GraphicsAllocation* allocation)
{
    // Bail with invalid alignment
    if (!math::isPowerOf2(desc.alignment) || desc.alignment == 0)
    {
        return SLANG_E_INVALID_ARG;
    }

    // Bail with invalid size
    if (desc.size == 0 || desc.size % desc.alignment != 0)
    {
        return SLANG_E_INVALID_ARG;
    }

    // Select a page size to store the allocation
    uint32_t pageSize = 0;
    if (desc.size <= 1 * 1024 * 1024)
        pageSize = 8 * 1024 * 1024;
    else if (desc.size <= 8 * 1024 * 1024)
        pageSize = 64 * 1024 * 1024;
    else if (desc.size <= 64 * 1024 * 1024)
        pageSize = 256 * 1024 * 1024;
    else
        pageSize = math::calcAligned(desc.size, 256 * 1024 * 1024);


    // Find a page of the correct size + alignment with space in
    for (auto& page : m_pages)
    {
        if (page->m_desc.size == pageSize && page->m_desc.alignment == desc.alignment)
        {
            // Allocate from the page
            auto pageAllocation = page->m_allocator.allocate(desc.size / desc.alignment);
            if (pageAllocation)
            {
                *allocation =
                    {pageAllocation.offset * page->m_desc.alignment, desc.size, page, pageAllocation.metadata};
                return SLANG_OK;
            }
        }
    }

    // No suitable page found, create a new one
    PageDesc pageDesc;
    pageDesc.alignment = desc.alignment;
    pageDesc.size = pageSize;
    Page* newPage = nullptr;
    Result res = createPage(pageDesc, &newPage);
    if (res == SLANG_E_OUT_OF_MEMORY)
    {
        // Out of memory - try cleaning up existing free pages
        // before failing.
        SLANG_RETURN_ON_FAIL(cleanUp());
        res = createPage(pageDesc, &newPage);
    }
    SLANG_RETURN_ON_FAIL(res);

    // Allocate into the new page
    {
        auto pageAllocation = newPage->m_allocator.allocate(desc.size / desc.alignment);
        if (pageAllocation)
        {
            *allocation =
                {pageAllocation.offset * newPage->m_desc.alignment, desc.size, newPage, pageAllocation.metadata};
            return SLANG_OK;
        }
    }

    // Should never get here - means allocation into empty page failed.
    return SLANG_FAIL;
}

Result GraphicsHeap::retire(GraphicsAllocation allocation)
{
    Page* page = static_cast<Page*>(allocation.pageId);

    OffsetAllocator::Allocation pageAllocation = {
        (uint32_t)(allocation.offset / page->m_desc.alignment),
        allocation.nodeIndex
    };
    page->m_allocator.free(pageAllocation);

    return SLANG_OK;
}

Result GraphicsHeap::createPage(const PageDesc& desc, Page** page)
{
    // Ask platform implementation to allocate the page
    SLANG_RETURN_ON_FAIL(allocatePage(desc, page))

    // Assign an ID to the page and add to list
    (*page)->m_id = m_nextPageId++;
    m_pages.push_back(*page);

    return SLANG_OK;
}

Result GraphicsHeap::destroyPage(Page* page)
{
    // Remove from list
    auto it = std::find(m_pages.begin(), m_pages.end(), page);
    if (it != m_pages.end())
    {
        m_pages.erase(it);
    }

    // Use platform implementation to free the page
    return freePage(page);
}

Result GraphicsHeap::cleanUp()
{
    // Free all pages that are not in use
    for (auto it = m_pages.begin(); it != m_pages.end();)
    {
        Page* page = *it;
        if (page->m_allocator.getFreeStorage() == page->m_allocator.getSize())
        {
            // Free the page and remove it from the list
            SLANG_RETURN_ON_FAIL(destroyPage(page));
            it = m_pages.erase(it);
        }
        else
        {
            // Keep the page if not empty
            ++it;
        }
    }
    return SLANG_OK;
}

} // namespace rhi
