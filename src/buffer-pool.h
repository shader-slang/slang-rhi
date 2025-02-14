#pragma once

#include <slang-rhi.h>

#include "core/common.h"

#include <vector>

namespace rhi {

template<typename TDevice, typename TBuffer>
class BufferPool
{
public:
    struct StagingBufferPage
    {
        RefPtr<TBuffer> resource;
        size_t size;
    };

    struct Allocation
    {
        TBuffer* resource;
        size_t offset;
    };

    TDevice* m_device;
    MemoryType m_memoryType;
    uint32_t m_alignment;
    BufferUsage m_usage;

    std::vector<StagingBufferPage> m_pages;
    std::vector<RefPtr<TBuffer>> m_largeAllocations;

    size_t m_pageAllocCounter = 0;
    size_t m_offsetAllocCounter = 0;

    const size_t kStagingBufferDefaultPageSize = 16 * 1024 * 1024;

    void init(TDevice* device, MemoryType memoryType, uint32_t alignment, BufferUsage usage)
    {
        m_device = device;
        m_memoryType = memoryType;
        m_alignment = alignment;
        m_usage = usage;
    }

    static size_t alignUp(size_t value, uint32_t alignment) { return (value + alignment - 1) / alignment * alignment; }

    void reset()
    {
        m_pageAllocCounter = 0;
        m_offsetAllocCounter = 0;
        m_largeAllocations.clear();
    }

    Result newStagingBufferPage()
    {
        StagingBufferPage page;
        size_t pageSize = kStagingBufferDefaultPageSize;

        ComPtr<IBuffer> bufferPtr;
        BufferDesc bufferDesc;
        bufferDesc.usage = m_usage;
        bufferDesc.defaultState = ResourceState::General;
        bufferDesc.memoryType = m_memoryType;
        bufferDesc.size = pageSize;
        SLANG_RETURN_ON_FAIL(m_device->createBuffer(bufferDesc, nullptr, bufferPtr.writeRef()));

        page.resource = checked_cast<TBuffer*>(bufferPtr.get());
        // The buffer is owned by the pool.
        page.resource->comFree();
        page.size = pageSize;
        m_pages.push_back(page);
        return SLANG_OK;
    }

    Result newLargeBuffer(size_t size)
    {
        ComPtr<IBuffer> bufferPtr;
        BufferDesc bufferDesc;
        bufferDesc.usage = m_usage;
        bufferDesc.defaultState = ResourceState::General;
        bufferDesc.memoryType = m_memoryType;
        bufferDesc.size = size;
        SLANG_RETURN_ON_FAIL(m_device->createBuffer(bufferDesc, nullptr, bufferPtr.writeRef()));
        auto bufferImpl = checked_cast<TBuffer*>(bufferPtr.get());
        m_largeAllocations.push_back(bufferImpl);
        // The buffer is owned by the pool.
        m_largeAllocations.back()->comFree();
        return SLANG_OK;
    }

    Allocation allocate(size_t size, bool forceLargePage = false)
    {
        if (forceLargePage || size >= (kStagingBufferDefaultPageSize >> 2))
        {
            newLargeBuffer(size);
            Allocation result;
            result.resource = m_largeAllocations.back();
            result.offset = 0;
            return result;
        }

        size_t bufferAllocOffset = alignUp(m_offsetAllocCounter, m_alignment);
        Index bufferId = -1;
        for (GfxIndex i = m_pageAllocCounter; i < m_pages.size(); i++)
        {
            auto cb = m_pages[i].resource.Ptr();
            if (bufferAllocOffset + size <= cb->m_desc.size)
            {
                bufferId = i;
                break;
            }
            bufferAllocOffset = 0;
        }
        // If we cannot find an existing page with sufficient free space,
        // create a new page.
        if (bufferId == -1)
        {
            newStagingBufferPage();
            bufferId = m_pages.size() - 1;
        }
        // Sub allocate from current page.
        Allocation result;
        result.resource = m_pages[bufferId].resource.Ptr();
        result.offset = bufferAllocOffset;
        m_pageAllocCounter = bufferId;
        m_offsetAllocCounter = bufferAllocOffset + size;
        return result;
    }
};

} // namespace rhi
