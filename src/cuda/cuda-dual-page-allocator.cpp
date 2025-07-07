#include "cuda-dual-page-allocator.h"
#include "cuda-device.h"
#include "cuda-buffer.h"

namespace rhi::cuda {

size_t nextPowerOf2(size_t n)
{
    size_t e = 0;
    size_t v = 1;
    while (v < n)
    {
        v <<= 1;
        e++;
    }
    return e;
}

DualPageAllocator::~DualPageAllocator()
{
    SLANG_RHI_ASSERT(m_totalAllocated == 0);
    auto res = reset();
    SLANG_RHI_ASSERT(SLANG_SUCCEEDED(res));
}

Result DualPageAllocator::init(DeviceImpl* device)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_device = device;
    return SLANG_OK;
}

Result DualPageAllocator::reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // Destroy all linked lists of pages
    for (auto& pageList : m_freePages)
    {
        for (auto& page : pageList)
        {
            SLANG_RETURN_ON_FAIL(destroyPage(page));
        }
        pageList.clear();
    }
    return SLANG_OK;
}


Result DualPageAllocator::allocate(size_t minSize, DualPageAllocator::Handle** handle)
{
    Page page;
    SLANG_RETURN_ON_FAIL(allocate(minSize, page));
    RefPtr<Handle> res = new Handle(this, page);
    returnRefPtr(handle, res);
    return SLANG_OK;
}

Result DualPageAllocator::allocate(size_t minSize, DualPageAllocator::Page& outPage)
{
    if (minSize == 0)
    {
        outPage = {};
        return SLANG_E_INVALID_ARG;
    }

    size_t idx = nextPowerOf2(minSize);
    if (idx >= 32)
    {
        return SLANG_E_OUT_OF_MEMORY;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

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
    }

    // Track total allocated
    m_totalAllocated += outPage.size;

    // Unless code is broken, the page should be big enough to fit 'minSize'
    SLANG_RHI_ASSERT(outPage.size >= minSize);
    return SLANG_OK;
}

Result DualPageAllocator::free(Page page)
{
    if (page.hostData == nullptr && page.deviceData == 0)
    {
        return SLANG_E_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    SLANG_RHI_ASSERT(m_totalAllocated > 0);
    m_totalAllocated -= page.size;

    // Add the page to the free list for its size
    m_freePages[page.idx].push_back(page);
    return SLANG_OK;
}


Result DualPageAllocator::createPage(size_t powerOf2, Page& outPage)
{
    size_t size = 1ULL << powerOf2;
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAllocHost(&outPage.hostData, size), m_device);
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuMemAlloc(&outPage.deviceData, size), m_device);
    outPage.size = size;
    outPage.idx = powerOf2;
    return SLANG_OK;
}

Result DualPageAllocator::destroyPage(Page page)
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
