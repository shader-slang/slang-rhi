#include "wgpu-constant-buffer-pool.h"
#include "wgpu-device.h"
#include "wgpu-buffer.h"

namespace rhi::wgpu {

inline size_t alignUp(size_t value, size_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

void ConstantBufferPool::init(DeviceImpl* device)
{
    m_device = device;
}

void ConstantBufferPool::upload(Context& ctx, WGPUQueue queue)
{
    auto uploadPage = [&](const Page& page)
    {
        if (page.usedSize > 0)
        {
            ctx.api.wgpuQueueWriteBuffer(queue, page.buffer->m_buffer, 0, page.data.data(), page.usedSize);
        }
    };
    for (const auto& page : m_pages)
    {
        uploadPage(page);
    }
    for (const auto& page : m_largePages)
    {
        uploadPage(page);
    }
}

void ConstantBufferPool::reset()
{
    for (auto& page : m_pages)
    {
        page.usedSize = 0;
    }
    for (auto& page : m_largePages)
    {
        page.usedSize = 0;
    }
    m_currentPage = -1;
    m_currentLargePage = 0;
    m_currentOffset = 0;
}

Result ConstantBufferPool::allocate(size_t size, Allocation& outAllocation)
{
    if (size > kPageSize)
    {
        size_t pageIndex = m_currentLargePage;
        while (pageIndex < m_largePages.size() && m_largePages[pageIndex].size < size)
        {
            ++pageIndex;
        }
        if (pageIndex == m_largePages.size())
        {
            m_largePages.push_back(Page());
            size_t pageSize = alignUp(size, kAlignment);
            SLANG_RETURN_ON_FAIL(createPage(pageSize, m_largePages.back()));
            pageIndex = m_largePages.size() - 1;
        }
        if (pageIndex != m_currentLargePage)
        {
            std::swap(m_largePages[pageIndex], m_largePages[m_currentLargePage]);
        }

        Page& page = m_largePages[m_currentLargePage++];
        page.usedSize = alignUp(size, kAlignment);
        outAllocation.buffer = page.buffer;
        outAllocation.offset = 0;
        outAllocation.mappedData = page.data.data();
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
    outAllocation.buffer = page.buffer;
    outAllocation.offset = m_currentOffset;
    outAllocation.mappedData = page.data.data() + m_currentOffset;
    m_currentOffset = alignUp(m_currentOffset + size, kAlignment);
    page.usedSize = m_currentOffset;
    return SLANG_OK;
}

Result ConstantBufferPool::createPage(size_t size, Page& outPage)
{
    ComPtr<IBuffer> buffer;
    BufferDesc bufferDesc;
    bufferDesc.usage = BufferUsage::ConstantBuffer | BufferUsage::CopyDestination;
    bufferDesc.defaultState = ResourceState::ConstantBuffer;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    bufferDesc.size = size;
    SLANG_RETURN_ON_FAIL(m_device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));

    outPage.buffer = checked_cast<BufferImpl*>(buffer.get());
    outPage.data.resize(size);
    outPage.size = size;
    outPage.usedSize = 0;
    return SLANG_OK;
}

} // namespace rhi::wgpu
