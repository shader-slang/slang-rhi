#include "testing.h"

#include <string>
#include <map>
#include <functional>
#include <memory>
#include <random>
#include <thread>

#include "rhi-shared.h"

using namespace rhi;
using namespace rhi::testing;

static const Size kPageSize = 16 * 1024 * 1024;

GPU_TEST_CASE("staging-heap-alloc-free", ALL)
{
    StagingHeap heap;
    heap.initialize((Device*)device.get(), kPageSize, MemoryType::Upload);

    Size allocSize = heap.alignUp(16);

    CHECK_EQ(heap.getUsed(), 0);
    CHECK_EQ(heap.getNumPages(), 0);

    StagingHeap::Allocation allocation;
    heap.alloc(16, {2}, &allocation);
    heap.checkConsistency();

    CHECK_EQ(allocation.getOffset(), 0);
    CHECK_EQ(allocation.getSize(), allocSize);
    CHECK_EQ(allocation.getMetaData().use, 2);
    CHECK_EQ(allocation.getPageId(), 1);
    CHECK_EQ(heap.getNumPages(), 1);
    CHECK_EQ(heap.getUsed(), allocSize);

    StagingHeap::Allocation allocation2;
    heap.alloc(16, {3}, &allocation2);
    heap.checkConsistency();

    CHECK_EQ(allocation2.getOffset(), allocSize);
    CHECK_EQ(allocation2.getSize(), allocSize);
    CHECK_EQ(allocation2.getMetaData().use, 3);
    CHECK_EQ(allocation2.getPageId(), 1);
    CHECK_EQ(heap.getNumPages(), 1);
    CHECK_EQ(heap.getUsed(), allocSize * 2);

    heap.free(allocation);
    heap.checkConsistency();

    CHECK_EQ(heap.getUsed(), allocSize);

    heap.free(allocation2);
    heap.checkConsistency();

    CHECK_EQ(heap.getUsed(), 0);
    CHECK_EQ(heap.getNumPages(), 1); // Should keep 1 empty page around
}

GPU_TEST_CASE("staging-heap-large-page", ALL)
{
    StagingHeap heap;
    heap.initialize((Device*)device.get(), kPageSize, MemoryType::Upload);

    StagingHeap::Allocation allocation;
    heap.alloc(16, {2}, &allocation);
    heap.checkConsistency();
    CHECK_EQ(allocation.getOffset(), 0);
    CHECK_EQ(allocation.getPageId(), 1);

    StagingHeap::Allocation bigAllocation;
    heap.alloc(heap.getPageSize() + 1, {2}, &bigAllocation);
    heap.checkConsistency();
    CHECK_EQ(bigAllocation.getOffset(), 0);
    CHECK_EQ(bigAllocation.getPageId(), 2);

    StagingHeap::Allocation allocation2;
    heap.alloc(16, {2}, &allocation2);
    heap.checkConsistency();
    CHECK_EQ(allocation2.getOffset(), heap.getAlignment());
    CHECK_EQ(allocation2.getPageId(), 1);

    StagingHeap::Allocation bigAllocation2;
    heap.alloc(heap.getPageSize() + 1, {2}, &bigAllocation2);
    heap.checkConsistency();
    CHECK_EQ(bigAllocation2.getOffset(), 0);
    CHECK_EQ(bigAllocation2.getPageId(), 3);

    StagingHeap::Allocation allocation3;
    heap.alloc(16, {2}, &allocation3);
    heap.checkConsistency();
    CHECK_EQ(allocation3.getOffset(), heap.getAlignment() * 2);
    CHECK_EQ(allocation3.getPageId(), 1);
}

GPU_TEST_CASE("staging-heap-realloc", ALL)
{
    StagingHeap heap;
    heap.initialize((Device*)device.get(), kPageSize, MemoryType::Upload);

    Size allocSize = heap.getPageSize() / 16;

    // Allocate a page's worth of memory in 16 chunks.
    std::vector<StagingHeap::Allocation> allocations;
    for (Size i = 0; i < 16; i++)
    {
        StagingHeap::Allocation allocation;
        heap.alloc(allocSize, {(int)i}, &allocation);
        heap.checkConsistency();
        CHECK_EQ(allocation.getOffset(), i * allocSize);
        CHECK_EQ(allocation.getPageId(), 1);
        allocations.push_back(allocation);
    }

    // Free chunks 3 and 4.
    heap.free(allocations[3]);
    heap.checkConsistency();
    heap.free(allocations[4]);
    heap.checkConsistency();

    // Make a new allocation that should reuse the free space.
    StagingHeap::Allocation allocation;
    heap.alloc(allocSize * 2, {2}, &allocation);
    heap.checkConsistency();
    CHECK_EQ(allocation.getOffset(), 3 * allocSize);
    CHECK_EQ(allocation.getPageId(), 1);
}

GPU_TEST_CASE("staging-heap-handles", ALL)
{
    StagingHeap heap;
    heap.initialize((Device*)device.get(), kPageSize, MemoryType::Upload);

    // Make an allocation using ref counted handle within a scope.
    {
        RefPtr<StagingHeap::Handle> handle;
        heap.allocHandle(16, {2}, handle.writeRef());
        heap.checkConsistency();
        CHECK_EQ(handle->getOffset(), 0);
        CHECK_EQ(handle->getPageId(), 1);
        CHECK_EQ(heap.getUsed(), heap.getAlignment());
    }

    // Allocation should be freed when handle goes out of scope.
    CHECK_EQ(heap.getUsed(), 0);
}

void thrashHeap(Device* device, StagingHeap* heap, int idx)
{
    std::vector<StagingHeap::Allocation> allocations;
    for (int i = 0; i < 1000; i++)
    {
        StagingHeap::Allocation allocation;
        heap->alloc(16, {idx}, &allocation);
        allocations.push_back(allocation);
    }
    heap->checkConsistency();
    for (auto& allocation : allocations)
    {
        heap->free(allocation);
    }
    heap->checkConsistency();
}

GPU_TEST_CASE("staging-heap-mutithreading", ALL)
{
    Device* deviceImpl = (Device*)device.get();

    StagingHeap heap;
    heap.initialize(deviceImpl, kPageSize, MemoryType::Upload);

    std::thread t1(thrashHeap, deviceImpl, &heap, 1);
    std::thread t2(thrashHeap, deviceImpl, &heap, 2);
    std::thread t3(thrashHeap, deviceImpl, &heap, 3);
    std::thread t4(thrashHeap, deviceImpl, &heap, 4);
    std::thread t5(thrashHeap, deviceImpl, &heap, 5);
    std::thread t6(thrashHeap, deviceImpl, &heap, 6);

    t1.join();
    t2.join();
    t4.join();
    t3.join();
    t5.join();
    t6.join();

    heap.checkConsistency();
}

void doTenAllocations(Device* device, StagingHeap* heap, int idx)
{
    std::vector<StagingHeap::Allocation> allocations;
    for (int i = 0; i < 10; i++)
    {
        StagingHeap::Allocation allocation;
        heap->alloc(16, {idx}, &allocation);
        allocations.push_back(allocation);
    }
}

GPU_TEST_CASE("staging-heap-threadlock-pages", ALL)
{
    Device* deviceImpl = (Device*)device.get();

    // When pages AREN'T being kept mapped, heap should allocate a new
    // page for each thread. As a result, after 3 threads have done 10
    // allocations we should have 3 pages.

    StagingHeap heap;
    heap.initialize(deviceImpl, kPageSize, MemoryType::Upload);
    heap.testOnlySetKeepPagesMapped(false);

    std::thread t1(doTenAllocations, deviceImpl, &heap, 1);
    std::thread t2(doTenAllocations, deviceImpl, &heap, 2);
    std::thread t3(doTenAllocations, deviceImpl, &heap, 3);

    t1.join();
    t2.join();
    t3.join();

    heap.checkConsistency();

    CHECK_EQ(heap.getNumPages(), 3);
}

GPU_TEST_CASE("staging-heap-shared-pages", ALL)
{
    Device* deviceImpl = (Device*)device.get();

    // When pages ARE being kept mapped, heap should share pages
    // between threads, so 10 small allocations from 3 threads should
    // all fit in the same page.

    StagingHeap heap;
    heap.initialize(deviceImpl, kPageSize, MemoryType::Upload);
    heap.testOnlySetKeepPagesMapped(true);

    std::thread t1(doTenAllocations, deviceImpl, &heap, 1);
    std::thread t2(doTenAllocations, deviceImpl, &heap, 2);
    std::thread t3(doTenAllocations, deviceImpl, &heap, 3);

    t1.join();
    t2.join();
    t3.join();

    heap.checkConsistency();

    CHECK_EQ(heap.getNumPages(), 1);
}

GPU_TEST_CASE("staging-heap-unlockpage-1", ALL)
{
    Device* deviceImpl = (Device*)device.get();

    // Verify that in none sharing mode, when this thread and another
    // one attempt to allocate, we end up with 2 pages (effectively
    // same as staging-heap-threadlock-pages but with local thread).

    StagingHeap heap;
    heap.initialize(deviceImpl, kPageSize, MemoryType::Upload);
    heap.testOnlySetKeepPagesMapped(false);

    StagingHeap::Allocation alloc;
    heap.alloc(16, {1}, &alloc);

    std::thread t1(doTenAllocations, deviceImpl, &heap, 1);

    t1.join();

    heap.checkConsistency();

    CHECK_EQ(heap.getNumPages(), 2);
}

GPU_TEST_CASE("staging-heap-unlockpage-2", ALL)
{
    Device* deviceImpl = (Device*)device.get();

    // Verify that if staging-heap-unlockpage-1 is repeated, but
    // the current thread frees its allocation, the 2nd thread
    // will reuse the page.

    StagingHeap heap;
    heap.initialize(deviceImpl, kPageSize, MemoryType::Upload);
    heap.testOnlySetKeepPagesMapped(false);

    StagingHeap::Allocation alloc;
    heap.alloc(16, {1}, &alloc);
    heap.free(alloc);

    std::thread t1(doTenAllocations, deviceImpl, &heap, 1);

    t1.join();

    heap.checkConsistency();

    CHECK_EQ(heap.getNumPages(), 1);
}
