#include "cuda-constant-buffer-pool.h"
#include "cuda-device.h"
#include "cuda-buffer.h"
#include "cuda-utils.h"

namespace rhi::cuda {

inline size_t alignUp(size_t value, size_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
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
            SLANG_CUDA_ASSERT_ON_FAIL(
                cuMemcpyHtoDAsync(page.handle->getDevicePtr(), page.handle->getHostPtr(), page.usedSize, stream)
            );
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
    m_pages.clear();
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
        outAllocation.hostData = page.handle->getHostPtr();
        outAllocation.deviceData = page.handle->getDevicePtr();
        return SLANG_OK;
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
    outAllocation.hostData = reinterpret_cast<uint8_t*>(page.handle->getHostPtr()) + m_currentOffset;
    outAllocation.deviceData = page.handle->getDevicePtr() + m_currentOffset;
    m_currentOffset = alignUp(m_currentOffset + size, kAlignment);
    page.usedSize = m_currentOffset;
    return SLANG_OK;
}

Result ConstantBufferPool::createPage(size_t size, Page& outPage)
{
    SLANG_RETURN_ON_FAIL(m_device->m_dualPageAllocator.allocate(size, outPage.handle.writeRef()));
    outPage.usedSize = 0;
    return SLANG_OK;
}

} // namespace rhi::cuda
