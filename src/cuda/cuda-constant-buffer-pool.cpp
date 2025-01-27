#include "cuda-constant-buffer-pool.h"
#include "cuda-device.h"
#include "cuda-buffer.h"

namespace rhi::cuda {

inline size_t alignUp(size_t value, size_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

ConstantBufferPool::~ConstantBufferPool()
{
    for (auto& page : m_pages)
    {
        destroyPage(page);
    }
    for (auto& page : m_largePages)
    {
        destroyPage(page);
    }
}

void ConstantBufferPool::init(DeviceImpl* device)
{
    m_device = device;
}

void ConstantBufferPool::upload(CUstream stream)
{
    auto uploadPage = [&](Page& page)
    {
        if (page.usedSize > 0)
        {
            SLANG_CUDA_ASSERT_ON_FAIL(cuMemcpyHtoDAsync(page.deviceData, page.hostData, page.usedSize, stream));
        }
    };

    for (auto& page : m_pages)
    {
        uploadPage(page);
    }
    for (auto& page : m_largePages)
    {
        uploadPage(page);
    }
}

void ConstantBufferPool::reset()
{
    m_currentPage = -1;
    m_currentOffset = 0;
    for (auto& page : m_largePages)
    {
        destroyPage(page);
    }
    m_largePages.clear();
}

Result ConstantBufferPool::allocate(size_t size, Allocation& outAllocation)
{
    if (size > kPageSize)
    {
        m_largePages.push_back(Page());
        Page& page = m_largePages.back();
        SLANG_RETURN_ON_FAIL(createPage(size, page));
        page.usedSize = size;
        outAllocation.hostData = page.hostData;
        outAllocation.deviceData = page.deviceData;
        return SLANG_FAIL;
    }

    if (m_currentPage == -1 || m_currentOffset + size > kPageSize)
    {
        m_currentPage += 1;
        if (m_currentPage >= int(m_pages.size()))
        {
            m_pages.push_back(Page());
            SLANG_RETURN_ON_FAIL(createPage(kPageSize, m_pages.back()));
        }
        m_currentOffset = 0;
    }

    Page& page = m_pages[m_currentPage];
    SLANG_RHI_ASSERT(page.hostData != nullptr);
    SLANG_RHI_ASSERT(page.deviceData != 0);
    outAllocation.hostData = page.hostData + m_currentOffset;
    outAllocation.deviceData = page.deviceData + m_currentOffset;
    m_currentOffset = alignUp(m_currentOffset + size, kAlignment);
    page.usedSize = m_currentOffset;
    return SLANG_OK;
}

Result ConstantBufferPool::createPage(size_t size, Page& outPage)
{
    outPage.hostData = (uint8_t*)::malloc(size);
    if (!outPage.hostData)
    {
        return SLANG_FAIL;
    }
    SLANG_CUDA_RETURN_ON_FAIL(cuMemAlloc(&outPage.deviceData, size));
    outPage.size = size;
    return SLANG_OK;
}

void ConstantBufferPool::destroyPage(Page& page)
{
    if (page.hostData)
    {
        ::free(page.hostData);
    }
    if (page.deviceData)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuMemFree(page.deviceData));
    }
}

} // namespace rhi::cuda
