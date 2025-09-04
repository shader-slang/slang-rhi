#include "heap.h"

#include "rhi-shared.h"
#include "core/string.h"

#include <algorithm>


namespace rhi {

Result Heap::allocate(const HeapAllocDesc& desc_, HeapAlloc* outAllocation)
{
    // Allow device implementation to fix up descriptor
    HeapAllocDesc desc = desc_;
    SLANG_RETURN_ON_FAIL(fixUpAllocDesc(desc));

    // Bail with invalid alignment
    if (!math::isPowerOf2(desc.alignment) || desc.alignment == 0)
    {
        return SLANG_E_INVALID_ARG;
    }

    // Round up size
    Size size = math::calcAligned2(desc.size, desc.alignment);


    // Select a page size to store the allocation
    uint32_t pageSize = 0;
    if (size <= 1 * 1024 * 1024)
        pageSize = 8 * 1024 * 1024;
    else if (size <= 8 * 1024 * 1024)
        pageSize = 64 * 1024 * 1024;
    else if (size <= 64 * 1024 * 1024)
        pageSize = 256 * 1024 * 1024;
    else
        pageSize = math::calcAligned(size, 256 * 1024 * 1024);


    // Find a page of the correct size + alignment with space in
    for (auto& page : m_pages)
    {
        if (page->m_desc.size == pageSize && page->m_desc.alignment == desc.alignment)
        {
            // Allocate from the page
            auto pageAllocation = page->m_allocator.allocate(size / desc.alignment);
            if (pageAllocation)
            {
                Size offset = pageAllocation.offset * page->m_desc.alignment;
                *outAllocation = {offset, size, page, pageAllocation.metadata, page->offsetToAddress(offset)};
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
        SLANG_RETURN_ON_FAIL(removeEmptyPages());
        res = createPage(pageDesc, &newPage);
    }
    SLANG_RETURN_ON_FAIL(res);

    // Allocate into the new page
    {
        auto pageAllocation = newPage->m_allocator.allocate(size / desc.alignment);
        if (pageAllocation)
        {
            Size offset = pageAllocation.offset * newPage->m_desc.alignment;
            *outAllocation = {offset, size, newPage, pageAllocation.metadata, newPage->offsetToAddress(offset)};
            return SLANG_OK;
        }
    }

    // Should never get here - means allocation into empty page failed.
    return SLANG_FAIL;
}

Result Heap::retire(HeapAlloc allocation)
{
    Page* page = static_cast<Page*>(allocation.pageId);

    OffsetAllocator::Allocation pageAllocation = {
        (uint32_t)(allocation.offset / page->m_desc.alignment),
        allocation.nodeIndex
    };
    page->m_allocator.free(pageAllocation);

    return SLANG_OK;
}

Result Heap::createPage(const PageDesc& desc, Page** outPage)
{
    // Ask platform implementation to allocate the page
    SLANG_RETURN_ON_FAIL(allocatePage(desc, outPage))

    // Assign an ID to the page and add to list
    (*outPage)->m_id = m_nextPageId++;
    m_pages.push_back(*outPage);

    return SLANG_OK;
}

Result Heap::destroyPage(Page* page)
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

Result Heap::removeEmptyPages()
{
    // Free all pages that are not in use
    for (auto it = m_pages.begin(); it != m_pages.end();)
    {
        Page* page = *it;
        if (page->m_allocator.getFreeStorage() == page->m_allocator.getSize())
        {
            // Free the page and remove it from the list
            SLANG_RETURN_ON_FAIL(freePage(page));
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

Result Heap::report(HeapReport* outReport)
{
    HeapReport res;

    // Copy the heap's label to the report label field
    if (m_desc.label && *m_desc.label)
    {
        string::copy_safe(res.label, sizeof(res.label), m_desc.label);
    }
    else
    {
        string::copy_safe(res.label, sizeof(res.label), "Unnamed Heap");
    }

    for (Page* page : m_pages)
    {
        res.totalAllocated +=
            (page->m_allocator.getSize() - page->m_allocator.getFreeStorage()) * page->m_desc.alignment;
        res.totalMemUsage += page->m_desc.size;
        res.numAllocations += page->m_allocator.getCurrentAllocs();
        res.numPages++;
    }

    *outReport = res;
    return SLANG_OK;
}

} // namespace rhi
