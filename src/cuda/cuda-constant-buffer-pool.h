#pragma once

#include "cuda-base.h"

#include <vector>

namespace rhi::cuda {

enum class ConstantBufferMemType
{
    Global,
    EntryPoint,
};

class ConstantBufferPool
{
public:
    struct Allocation
    {
        void* hostData;
        CUdeviceptr deviceData;
    };

    ~ConstantBufferPool() { reset(); }

    void init(DeviceImpl* device);
    void upload(CUstream stream);
    void reset();

    Result allocate(size_t size, ConstantBufferMemType memType, Allocation& outAllocation);

private:
    // Note: Page size can be relatively small, as it is allocated from
    // the global device heap, which eventually handles small allocations.
    static constexpr size_t kAlignment = 64;
    static constexpr size_t kPageSize = 128 * 1024;
    static_assert(kPageSize % kAlignment == 0, "Page size must be a multiple of alignment");

    struct Page
    {
        HeapAlloc deviceMem;
        HeapAlloc hostMem;
        size_t usedSize = 0;
    };

    struct Pool
    {
        std::vector<Page> m_pages;
        std::vector<Page> m_largePages;

        int m_currentPage = -1;
        size_t m_currentOffset = 0;
        ConstantBufferMemType m_memType;

        Result allocate(DeviceImpl* device, size_t size, Allocation& outAllocation);
        Result createPage(DeviceImpl* device, size_t size, Page& outPage);
        void reset(DeviceImpl* device);
    };

    DeviceImpl* m_device;
    Pool m_globalDataPool;
    Pool m_entryPointDataPool;
};

} // namespace rhi::cuda
