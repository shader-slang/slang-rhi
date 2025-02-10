#pragma once

#include "wgpu-base.h"

#include <vector>

namespace rhi::wgpu {

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
    void upload(Context& ctx, WGPUCommandEncoder encoder);
    void reset();

    Result allocate(size_t size, Allocation& outAllocation);

private:
    static constexpr size_t kAlignment = 256;
    static constexpr size_t kPageSize = 4 * 1024 * 1024;

    struct Page
    {
        RefPtr<BufferImpl> buffer;
        RefPtr<BufferImpl> stagingBuffer;
        size_t size = 0;
        uint8_t* mappedData = nullptr;
        size_t usedSize = 0;
    };

    DeviceImpl* m_device;

    std::vector<Page> m_pages;
    std::vector<Page> m_largePages;

    int m_currentPage = -1;
    size_t m_currentOffset = 0;

    Result createPage(size_t size, Page& outPage);
    Result mapPage(Page& page);
    Result unmapPage(Page& page);
};

} // namespace rhi::wgpu
