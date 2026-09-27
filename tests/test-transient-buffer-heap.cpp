#include "testing.h"

#include "../src/device.h"
#include "../src/transient-buffer-heap.h"

#include <array>
#include <cstring>
#include <thread>

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("transient-buffer-arena-local-bump", D3D12 | Vulkan | DontCacheDevice)
{
    static constexpr Size kKiB = 1024;

    TransientBufferHeapDesc heapDesc;
    heapDesc.initialPageSize = 64 * kKiB;
    heapDesc.maxPageSize = 256 * kKiB;
    heapDesc.maxRetainedSize = 192 * kKiB;
    heapDesc.alignment = 256;
    heapDesc.allocationGranularity = 256;

    TransientBufferHeap heap;
    heap.initialize(getUnderlyingDevice(device.get()), heapDesc);

    TransientBufferArenaDesc arenaDesc;
    arenaDesc.initialChunkSize = 1 * kKiB;
    arenaDesc.maxChunkSize = 16 * kKiB;

    TransientBufferArena arena;
    arena.initialize(&heap, arenaDesc);

    for (uint32_t i = 0; i < 128; ++i)
    {
        TransientBufferArena::Allocation allocation;
        REQUIRE_CALL(arena.allocate(16, &allocation));
        CHECK(allocation.buffer);
        CHECK(allocation.mappedData);
        CHECK_EQ(allocation.offset % heapDesc.alignment, 0);
        CHECK_EQ(allocation.size, heapDesc.allocationGranularity);
        memset(allocation.mappedData, int(i), 16);
    }

    CHECK_LT(arena.getChunkCount(), 128);
    CHECK_EQ(arena.getUsed(), 128 * heapDesc.allocationGranularity);
    CHECK_EQ(heap.getNumPages(), 1);
    CHECK_GT(heap.getUsed(), arena.getUsed());
    CHECK_EQ(heap.getPageAllocationCount(), 1);

    arena.reset();
    CHECK_EQ(arena.getChunkCount(), 0);
    CHECK_EQ(arena.getUsed(), 0);
    CHECK_EQ(heap.getUsed(), 0);
    CHECK_EQ(heap.getCapacity(), heapDesc.initialPageSize);

    // A second wave reuses the warm physical page instead of allocating buffers again.
    for (uint32_t i = 0; i < 128; ++i)
    {
        TransientBufferArena::Allocation allocation;
        REQUIRE_CALL(arena.allocate(16, &allocation));
    }
    arena.reset();
    CHECK_EQ(heap.getPageAllocationCount(), 1);

    TransientBufferArena::Allocation oversizedAllocation;
    CHECK_EQ(arena.allocate(heapDesc.maxPageSize + 1, &oversizedAllocation), SLANG_FAIL);
    heap.release();
}

GPU_TEST_CASE("transient-buffer-heap-bounded-retention", D3D12 | Vulkan | DontCacheDevice)
{
    static constexpr Size kKiB = 1024;

    TransientBufferHeapDesc desc;
    desc.initialPageSize = 64 * kKiB;
    desc.maxPageSize = 256 * kKiB;
    desc.maxRetainedSize = 192 * kKiB;
    desc.alignment = 256;
    desc.allocationGranularity = 256;

    TransientBufferHeap heap;
    heap.initialize(getUnderlyingDevice(device.get()), desc);

    std::array<TransientBufferHeap::Chunk, 2> warmChunks;
    REQUIRE_CALL(heap.allocate(64 * kKiB, &warmChunks[0]));
    REQUIRE_CALL(heap.allocate(128 * kKiB, &warmChunks[1]));
    CHECK_EQ(heap.getCapacity(), 192 * kKiB);
    CHECK_EQ(heap.getPageAllocationCount(), 2);

    heap.free(warmChunks);
    CHECK_EQ(heap.getUsed(), 0);
    CHECK_EQ(heap.getCapacity(), desc.maxRetainedSize);

    TransientBufferHeap::Chunk oversizedChunk;
    REQUIRE_CALL(heap.allocate(256 * kKiB, &oversizedChunk));
    CHECK_EQ(heap.getPageAllocationCount(), 3);
    heap.free(std::span<TransientBufferHeap::Chunk>(&oversizedChunk, 1));

    // The oversized page is discarded first; the useful 64 KiB and 128 KiB pages stay warm.
    CHECK_EQ(heap.getUsed(), 0);
    CHECK_EQ(heap.getCapacity(), desc.maxRetainedSize);
    CHECK_EQ(heap.getNumPages(), 2);

    REQUIRE_CALL(heap.allocate(64 * kKiB, &warmChunks[0]));
    REQUIRE_CALL(heap.allocate(128 * kKiB, &warmChunks[1]));
    CHECK_EQ(heap.getPageAllocationCount(), 3);
    heap.free(warmChunks);
    heap.release();
}

GPU_TEST_CASE("transient-buffer-arena-parallel-recording", D3D12 | Vulkan | DontCacheDevice)
{
    static constexpr uint32_t kThreadCount = 4;
    static constexpr uint32_t kAllocationCount = 1024;
    static constexpr Size kKiB = 1024;

    TransientBufferHeapDesc heapDesc;
    heapDesc.initialPageSize = 64 * kKiB;
    heapDesc.maxPageSize = 512 * kKiB;
    heapDesc.maxRetainedSize = 1024 * kKiB;
    heapDesc.alignment = 256;
    heapDesc.allocationGranularity = 256;

    TransientBufferHeap heap;
    heap.initialize(getUnderlyingDevice(device.get()), heapDesc);

    std::array<TransientBufferArena, kThreadCount> arenas;
    for (auto& arena : arenas)
        arena.initialize(&heap);

    std::array<bool, kThreadCount> succeeded = {};
    std::array<std::thread, kThreadCount> threads;
    for (uint32_t threadIndex = 0; threadIndex < kThreadCount; ++threadIndex)
    {
        threads[threadIndex] = std::thread(
            [&, threadIndex]()
            {
                succeeded[threadIndex] = true;
                for (uint32_t i = 0; i < kAllocationCount; ++i)
                {
                    TransientBufferArena::Allocation allocation;
                    if (SLANG_FAILED(arenas[threadIndex].allocate(16, &allocation)) || !allocation.mappedData ||
                        allocation.offset % heapDesc.alignment != 0)
                    {
                        succeeded[threadIndex] = false;
                        return;
                    }
                    memset(allocation.mappedData, int(threadIndex), 16);
                }
            }
        );
    }
    for (auto& thread : threads)
        thread.join();

    size_t totalChunkCount = 0;
    for (uint32_t threadIndex = 0; threadIndex < kThreadCount; ++threadIndex)
    {
        CHECK(succeeded[threadIndex]);
        totalChunkCount += arenas[threadIndex].getChunkCount();
    }
    CHECK_LT(totalChunkCount, kThreadCount * kAllocationCount);
    CHECK_GT(heap.getUsed(), 0);

    for (auto& arena : arenas)
        arena.reset();
    CHECK_EQ(heap.getUsed(), 0);
    CHECK_LE(heap.getCapacity(), heapDesc.maxRetainedSize);
    heap.release();
}
