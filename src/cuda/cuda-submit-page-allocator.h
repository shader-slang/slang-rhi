#pragma once

#include "cuda-base.h"

#include <vector>

namespace rhi::cuda {

// Simple power of 2 based page allocator that:
// - Creates paired allocations in host and device memory
// - Automatically frees pages when associated event is triggered
// - Recycles free pages rather than releasing cuMemFree/cuMemFreeHost.
// On destruction, it blocks until all pending events are complete and
// then returns all pages to the free lists.
class SubmitPageAllocator
{
public:
    struct Page
    {
        void* hostData = nullptr;
        CUdeviceptr deviceData = 0;
        size_t size = 0;
        size_t idx = 0;
        Page* next = nullptr;
    };

    struct PageGroup
    {
        std::list<Page> pages;       // Linked list of pages in this group
        CUevent freeEvent = nullptr; // Event to signal when the group is free
    };

    ~SubmitPageAllocator();

    Result init(DeviceImpl* device);

    Result update();

    Result beginSubmit();
    Result endSubmit(CUstream stream);

    Result allocate(size_t size, Page& outPage);

private:
    DeviceImpl* m_device;

    // 32 linked lists of free pages, each for a given power of 2.
    std::list<Page> m_freePages[32];

    // Group for current submit
    PageGroup m_currentGroup;

    // List of active page groups
    std::list<PageGroup> m_activeGroups;

    // Create/destroy pages
    Result createPage(size_t powerOf2, Page& outPage);
    Result destroyPage(Page page);
};

} // namespace rhi::cuda
