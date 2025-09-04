#include "cuda-constant-buffer-pool.h"
#include "cuda-device.h"
#include "cuda-buffer.h"
#include "cuda-utils.h"
#include "cuda-heap.h"

namespace rhi::cuda {

inline size_t alignUp(size_t value, size_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

void ConstantBufferPool::init(DeviceImpl* device)
{
    m_device = device;

    // Set memory types for each pool
    m_globalDataPool.m_memType = ConstantBufferMemType::Global;
    m_entryPointDataPool.m_memType = ConstantBufferMemType::EntryPoint;
}

void ConstantBufferPool::upload(CUstream stream)
{
    auto uploadPage = [&](Page& page)
    {
        if (page.usedSize > 0)
        {
            SLANG_CUDA_ASSERT_ON_FAIL(
                cuMemcpyHtoDAsync(page.deviceMem.getDeviceAddress(), page.hostMem.getHostPtr(), page.usedSize, stream)
            );
        }
    };

    // Only upload global data
    for (auto& page : m_globalDataPool.m_pages)
    {
        uploadPage(page);
    }
    for (auto& page : m_globalDataPool.m_largePages)
    {
        uploadPage(page);
    }
}

void ConstantBufferPool::reset()
{
    m_entryPointDataPool.reset(m_device);
    m_globalDataPool.reset(m_device);
}

Result ConstantBufferPool::allocate(size_t size, ConstantBufferMemType memType, Allocation& outAllocation)
{
    if (memType == ConstantBufferMemType::Global)
    {
        return m_globalDataPool.allocate(m_device, size, outAllocation);
    }
    else
    {
        return m_entryPointDataPool.allocate(m_device, size, outAllocation);
    }
}

void ConstantBufferPool::Pool::reset(DeviceImpl* device)
{
    m_currentPage = -1;
    m_currentOffset = 0;
    for (auto& page : m_pages)
    {
        if (page.deviceMem)
            device->m_deviceMemHeap->free(page.deviceMem);
        if (page.hostMem)
            device->m_hostMemHeap->free(page.hostMem);
    }
    for (auto& page : m_largePages)
    {
        if (page.deviceMem)
            device->m_deviceMemHeap->free(page.deviceMem);
        if (page.hostMem)
            device->m_hostMemHeap->free(page.hostMem);
    }
    m_pages.clear();
    m_largePages.clear();
}

Result ConstantBufferPool::Pool::allocate(DeviceImpl* device, size_t size, Allocation& outAllocation)
{
    if (size == 0)
    {
        outAllocation.hostData = nullptr;
        outAllocation.deviceData = 0;
        return SLANG_OK;
    }

    if (size > kPageSize)
    {
        m_largePages.push_back(Page());
        Page& page = m_largePages.back();
        SLANG_RETURN_ON_FAIL(createPage(device, size, page));
        page.usedSize = size;
        outAllocation.hostData = page.hostMem ? page.hostMem.getHostPtr() : 0;
        outAllocation.deviceData = page.deviceMem ? page.deviceMem.getDeviceAddress() : 0;
        return SLANG_OK;
    }

    if (m_currentPage == -1 || m_currentOffset + size > kPageSize)
    {
        m_currentPage += 1;
        if (m_currentPage >= int(m_pages.size()))
        {
            m_pages.push_back(Page());
            SLANG_RETURN_ON_FAIL(createPage(device, kPageSize, m_pages.back()));
        }
        m_currentOffset = 0;
    }

    Page& page = m_pages[m_currentPage];
    outAllocation.hostData = page.hostMem ? reinterpret_cast<uint8_t*>(page.hostMem.getHostPtr()) + m_currentOffset : 0;
    outAllocation.deviceData = page.deviceMem ? page.deviceMem.getDeviceAddress() + m_currentOffset : 0;
    m_currentOffset = alignUp(m_currentOffset + size, kAlignment);
    page.usedSize = m_currentOffset;
    return SLANG_OK;
}

Result ConstantBufferPool::Pool::createPage(DeviceImpl* device, size_t size, Page& outPage)
{
    HeapAllocDesc desc;
    desc.alignment = kAlignment;
    desc.size = size;

    if (m_memType == ConstantBufferMemType::Global)
    {
        // Global data needs both host and device memory
        SLANG_RETURN_ON_FAIL(device->m_deviceMemHeap->allocate(desc, &outPage.deviceMem));
        SLANG_RETURN_ON_FAIL(device->m_hostMemHeap->allocate(desc, &outPage.hostMem));
    }
    else // ConstantBufferMemType::EntryPoint
    {
        // Entry point only needs host memory
        SLANG_RETURN_ON_FAIL(device->m_hostMemHeap->allocate(desc, &outPage.hostMem));
    }

    outPage.usedSize = 0;
    return SLANG_OK;
}

} // namespace rhi::cuda
