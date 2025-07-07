#include "cuda-submit-page-allocator.h"
#include "cuda-device.h"
#include "cuda-buffer.h"

namespace rhi::cuda {

size_t nextPowerOf2(size_t n)
{
    if (n == 0)
        return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    n++;
    return n;
}

SubmitPageAllocator::~SubmitPageAllocator()
{
    // Block until all pending events are done
    for (auto group : m_activeGroups)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuEventSynchronize(group.freeEvent));
    }

    // Update, which (now that all events are done) should end with no
    // active groups.
    update();
    SLANG_RHI_ASSERT(m_activeGroups.empty());

    // Destroy all linked lists of pages
    for (auto& pageList : m_freePages)
    {
        for (auto& page : pageList)
        {
            destroyPage(page);
        }
        pageList.clear();
    }

    // There's no safe way to handle the situation in which the allocator
    // is destructed half way through submission, but assert so the user
    // is at least made aware of it
    SLANG_RHI_ASSERT(m_currentGroup.freeEvent == nullptr && m_currentGroup.pages.empty());
}

Result SubmitPageAllocator::init(DeviceImpl* device)
{
    m_device = device;
    return SLANG_OK;
}

Result SubmitPageAllocator::update()
{
    for (auto groupIt = m_activeGroups.begin(); groupIt != m_activeGroups.end();)
    {
        CUresult result = cuEventQuery(groupIt->freeEvent);
        if (result == CUDA_SUCCESS)
        {
            // Event is passed, return all pages in group to the correct free list,
            // destroy the event and remove the group.
            for (auto& page : groupIt->pages)
                m_freePages[page.idx].push_back(page);
            cuEventDestroy(groupIt->freeEvent);
            groupIt = m_activeGroups.erase(groupIt);
        }
        else if (result == CUDA_ERROR_NOT_READY)
        {
            // Event is valid but has not yet been triggered.
        }
        else
        {
            // Error
            SLANG_CUDA_RETURN_ON_FAIL_REPORT(result, m_device);
        }
    }
    return SLANG_OK;
}

Result SubmitPageAllocator::beginSubmit()
{
    if (m_currentGroup.freeEvent != nullptr)
    {
        // If we already have a free event, we must have called beginSubmit() before.
        return SLANG_FAIL;
    }

    // It's a bug if freeEvent is null but pages aren't empty.
    SLANG_RHI_ASSERT(m_currentGroup.pages.empty());

    // Always update the allocator at start of submit to check if any pages can be freed
    SLANG_RETURN_ON_FAIL(update());

    // Create a new event for the current group
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuEventCreate(&m_currentGroup.freeEvent, 0), m_device);

    return SLANG_OK;
}

Result SubmitPageAllocator::endSubmit(CUstream stream)
{
    if (m_currentGroup.freeEvent == nullptr)
    {
        // If we don't have a free event, we must not have called beginSubmit() before.
        return SLANG_FAIL;
    }

    // Record the event on the stream
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuEventRecord(m_currentGroup.freeEvent, stream), m_device);

    // Add the group to the list of active groups
    m_activeGroups.push_back(std::move(m_currentGroup));

    // Reset current group for next submit
    m_currentGroup = PageGroup();

    return SLANG_OK;
}

Result SubmitPageAllocator::allocate(size_t size, Page& outPage)
{
    if (size == 0)
    {
        outPage = {};
        return SLANG_OK;
    }

    size_t idx = nextPowerOf2(size);
    if (idx >= 32)
    {
        return SLANG_E_OUT_OF_MEMORY;
    }

    // If current group has no free event, user has failed to call beginSubmit() before allocate().
    if (m_currentGroup.freeEvent == nullptr)
    {
        return SLANG_FAIL;
    }

    // Get existing or create new page.
    if (m_freePages[idx].empty())
    {
        // No free pages of this size, create a new one
        // This handles error carefully, so if it fails we
        // don't leak memory.
        outPage = {};
        auto res = createPage(idx, outPage);
        if (SLANG_FAILED(res))
        {
            destroyPage(outPage);
            outPage = {};
        }
        SLANG_RETURN_ON_FAIL(res);
    }
    else
    {
        // Take a page from the free list
        outPage = m_freePages[idx].front();
        m_freePages[idx].pop_front();
        m_currentGroup.pages.push_back(outPage);
    }

    // Unless code is broken, the page should be big enough to fit 'size'
    SLANG_RHI_ASSERT(outPage.size >= size);

    // Add to the current group
    m_currentGroup.pages.push_back(std::move(outPage));

    return SLANG_OK;
}

Result SubmitPageAllocator::createPage(size_t powerOf2, Page& outPage)
{
    size_t size = 1ULL << powerOf2;
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAllocHost(&outPage.hostData, size), m_device);
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAlloc(&outPage.deviceData, size), m_device);
    outPage.size = size;
    outPage.idx = powerOf2;
    return SLANG_OK;
}

Result SubmitPageAllocator::destroyPage(Page page)
{
    if (page.hostData)
    {
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemFreeHost(page.hostData), m_device);
    }
    if (page.deviceData)
    {
        SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemFree(page.deviceData), m_device);
    }
    return SLANG_OK;
}

} // namespace rhi::cuda
