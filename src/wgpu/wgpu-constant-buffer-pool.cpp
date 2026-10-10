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

void ConstantBufferPool::finish()
{
    auto writePage = [&](const Page& page)
    {
        if (page.usedSize > 0)
        {
            m_device->writeBuffer(page.buffer, 0, page.usedSize, page.data.get());
        }
    };
    for (const auto& page : m_pages)
    {
        writePage(page);
    }
    for (const auto& page : m_largePages)
    {
        writePage(page);
    }
}

void ConstantBufferPool::reset()
{
    for (auto& page : m_pages)
    {
        page.usedSize = 0;
    }
    m_largePages.clear();
    m_currentPage = -1;
    m_currentOffset = 0;
}

Result ConstantBufferPool::allocate(size_t size, Allocation& outAllocation)
{
    if (size > kPageSize)
    {
        m_largePages.push_back(Page());
        Page& page = m_largePages.back();
        SLANG_RETURN_ON_FAIL(createPage(size, page));
        page.usedSize = size;
        outAllocation.buffer = page.buffer;
        outAllocation.offset = 0;
        outAllocation.mappedData = page.data.get();
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
    outAllocation.mappedData = page.data.get() + m_currentOffset;
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
    outPage.data = std::make_unique<uint8_t[]>(size);
    outPage.size = size;
    outPage.usedSize = 0;
    return SLANG_OK;
}

} // namespace rhi::wgpu
