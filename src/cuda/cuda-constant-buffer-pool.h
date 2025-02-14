#pragma once

#include "cuda-base.h"

#include <vector>

namespace rhi::cuda {

class ConstantBufferPool
{
public:
    struct Allocation
    {
        void* hostData;
        CUdeviceptr deviceData;
    };

    ~ConstantBufferPool();

    void init(DeviceImpl* device);
    void upload(CUstream stream);
    void reset();

    Result allocate(size_t size, Allocation& outAllocation);

private:
    static constexpr size_t kAlignment = 64;
    static constexpr size_t kPageSize = 4 * 1024 * 1024;

    struct Page
    {
        uint8_t* hostData = nullptr;
        CUdeviceptr deviceData = 0;
        size_t size = 0;
        size_t usedSize = 0;
    };

    DeviceImpl* m_device;

    std::vector<Page> m_pages;
    std::vector<Page> m_largePages;

    int m_currentPage = -1;
    size_t m_currentOffset = 0;

    Result createPage(size_t size, Page& outPage);
    void destroyPage(Page& page);
};

} // namespace rhi::cuda
