#include "d3d12-constant-buffer-pool.h"
#include "d3d12-device.h"
#include "d3d12-buffer.h"

namespace rhi::d3d12 {

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
    for (auto& page : m_pages)
    {
        unmapPage(page);
    }
}

void ConstantBufferPool::reset()
{
    m_currentPage = -1;
    m_currentOffset = 0;
}

Result ConstantBufferPool::allocate(size_t size, Allocation& outAllocation)
{
    if (size > kPageSize)
    {
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
        SLANG_RETURN_ON_FAIL(mapPage(m_pages[m_currentPage]));
        m_currentOffset = 0;
    }

    const Page& page = m_pages[m_currentPage];
    SLANG_RHI_ASSERT(page.mappedData != nullptr);
    outAllocation.buffer = page.buffer;
    outAllocation.offset = m_currentOffset;
    outAllocation.mappedData = page.mappedData + m_currentOffset;
    m_currentOffset = alignUp(m_currentOffset + size, kAlignment);
    return SLANG_OK;
}

Result ConstantBufferPool::createPage(size_t size, Page& outPage)
{
    ComPtr<IBuffer> buffer;
    BufferDesc bufferDesc;
    bufferDesc.usage = BufferUsage::ConstantBuffer;
    bufferDesc.defaultState = ResourceState::ConstantBuffer;
    bufferDesc.memoryType = MemoryType::Upload;
    bufferDesc.size = size;
    SLANG_RETURN_ON_FAIL(m_device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));

    outPage.size = size;
    outPage.buffer = checked_cast<BufferImpl*>(buffer.get());
    // The buffer is owned by the pool.
    outPage.buffer->comFree();
    return SLANG_OK;
}

Result ConstantBufferPool::mapPage(Page& page)
{
    if (!page.mappedData)
    {
        SLANG_RETURN_ON_FAIL(m_device->mapBuffer(page.buffer, CpuAccessMode::Write, (void**)&page.mappedData));
        if (!page.mappedData)
        {
            return SLANG_FAIL;
        }
    }
    return SLANG_OK;
}

Result ConstantBufferPool::unmapPage(Page& page)
{
    if (page.mappedData)
    {
        SLANG_RETURN_ON_FAIL(m_device->unmapBuffer(page.buffer));
        page.mappedData = nullptr;
    }
    return SLANG_OK;
}

} // namespace rhi::d3d12
