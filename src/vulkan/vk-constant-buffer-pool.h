#pragma once

#include "vk-base.h"

#include <vector>

namespace rhi::vk {

class ConstantBufferPool
{
public:
    struct Allocation
    {
        BufferImpl* buffer;
        size_t offset;
        void* mappedData;
    };

    void init(DeviceImpl* device);
    void finish();
    void reset();

    Result allocate(size_t size, Allocation& outAllocation);

private:
    static constexpr size_t kPageSize = 4 * 1024 * 1024;

    struct Page
    {
        RefPtr<BufferImpl> buffer;
        size_t size = 0;
        uint8_t* mappedData = nullptr;
    };

    DeviceImpl* m_device;
    uint32_t m_alignment;

    std::vector<Page> m_pages;

    int m_currentPage = -1;
    size_t m_currentOffset = 0;

    Result createPage(size_t size, Page& outPage);
    Result mapPage(Page& page);
    Result unmapPage(Page& page);
};

} // namespace rhi::vk
