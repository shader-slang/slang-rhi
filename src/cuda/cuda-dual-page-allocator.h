#pragma once

#include "cuda-base.h"

#include <vector>
#include <mutex>
#include <thread>

namespace rhi::cuda {

// Simple power of 2 based page allocator that creates paired
// allocations in host and device memory, and provides a handle
// mechanism to allow pages to be freed using ref counted ptrs.
class DualPageAllocator
{
public:
    struct Page
    {
        void* hostData = nullptr;
        CUdeviceptr deviceData = 0;
        size_t size = 0;
        size_t idx = 0;
    };

    // Handle to a page that frees it when handle is freed.
    class Handle : public RefObject
    {
    public:
        Handle(DualPageAllocator* allocator, Page page)
            : m_allocator(allocator)
            , m_page(page)
        {
        }
        ~Handle() { m_allocator->free(m_page); }

        const Page& getPage() const { return m_page; }

        Size getSize() const { return m_page.size; }
        CUdeviceptr getDevicePtr() const { return m_page.deviceData; }
        void* getHostPtr() const { return m_page.hostData; }

    private:
        DualPageAllocator* m_allocator;
        Page m_page;
    };

    ~DualPageAllocator();

    Result init(DeviceImpl* device);
    Result reset();
    Result allocate(size_t minSize, Handle** handle);

private:
    DeviceImpl* m_device;
    mutable std::mutex m_mutex;
    size_t m_totalAllocated = 0;

    // 32 linked lists of free pages, each for a given power of 2.
    std::list<Page> m_freePages[32];

    Result allocate(size_t minSize, Page& outPage);
    Result free(Page page);

    // Create/destroy pages
    Result createPage(size_t powerOf2, Page& outPage);
    Result destroyPage(Page page);
};

} // namespace rhi::cuda
