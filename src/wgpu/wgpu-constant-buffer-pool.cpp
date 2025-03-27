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
    for (auto& page : m_pages)
    {
        unmapPage(page);
    }
    for (auto& page : m_largePages)
    {
        unmapPage(page);
    }
}

void ConstantBufferPool::upload(Context& ctx, WGPUCommandEncoder encoder)
{
    auto uploadPage = [&](const Page& page)
    {
        if (page.usedSize > 0)
        {
            ctx.api.wgpuCommandEncoderCopyBufferToBuffer(
                encoder,
                page.stagingBuffer->m_buffer,
                0,
                page.buffer->m_buffer,
                0,
                page.usedSize
            );
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
        SLANG_RETURN_ON_FAIL(mapPage(page));
        page.usedSize = size;
        outAllocation.buffer = page.buffer;
        outAllocation.offset = 0;
        outAllocation.mappedData = page.mappedData;
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
        mapPage(m_pages[m_currentPage]);
        m_currentOffset = 0;
    }

    Page& page = m_pages[m_currentPage];
    SLANG_RHI_ASSERT(page.mappedData != nullptr);
    outAllocation.buffer = page.buffer;
    outAllocation.offset = m_currentOffset;
    outAllocation.mappedData = page.mappedData + m_currentOffset;
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

    ComPtr<IBuffer> stagingBuffer;
    BufferDesc stagingBufferDesc;
    stagingBufferDesc.usage = BufferUsage::CopySource;
    stagingBufferDesc.defaultState = ResourceState::CopySource;
    stagingBufferDesc.memoryType = MemoryType::Upload;
    stagingBufferDesc.size = size;
    SLANG_RETURN_ON_FAIL(m_device->createBuffer(stagingBufferDesc, nullptr, stagingBuffer.writeRef()));


    outPage.buffer = checked_cast<BufferImpl*>(buffer.get());
    outPage.stagingBuffer = checked_cast<BufferImpl*>(stagingBuffer.get());
    // The buffers are owned by the pool.
    outPage.buffer->breakStrongReferenceToDevice();
    outPage.stagingBuffer->breakStrongReferenceToDevice();
    outPage.size = size;
    outPage.usedSize = 0;
    return SLANG_OK;
}

Result ConstantBufferPool::mapPage(Page& page)
{
    if (!page.mappedData)
    {
        SLANG_RETURN_ON_FAIL(m_device->mapBuffer(page.stagingBuffer, CpuAccessMode::Write, (void**)&page.mappedData));
    }
    return SLANG_OK;
}

Result ConstantBufferPool::unmapPage(Page& page)
{
    if (page.mappedData)
    {
        SLANG_RETURN_ON_FAIL(m_device->unmapBuffer(page.stagingBuffer));
        page.mappedData = nullptr;
    }
    return SLANG_OK;
}

} // namespace rhi::wgpu
