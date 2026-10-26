#pragma once

#include "wgpu-base.h"

#include <memory>
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
    /// Write the allocated data to the GPU buffers. Called when the command buffer is finished.
    void finish();
    void reset();

    Result allocate(size_t size, Allocation& outAllocation);

private:
    static constexpr size_t kAlignment = 256;
    static constexpr size_t kPageSize = 4 * 1024 * 1024;

    struct Page
    {
        InternalRefPtr<BufferImpl> buffer;
        // Constant data is written on the CPU and copied to the buffer with wgpuQueueWriteBuffer,
        // which avoids waiting for an asynchronous buffer mapping while recording commands.
        std::unique_ptr<uint8_t[]> data;
        size_t size = 0;
        size_t usedSize = 0;
    };

    DeviceImpl* m_device;

    std::vector<Page> m_pages;
    std::vector<Page> m_largePages;

    int m_currentPage = -1;
    size_t m_currentOffset = 0;

    Result createPage(size_t size, Page& outPage);
};

} // namespace rhi::wgpu
