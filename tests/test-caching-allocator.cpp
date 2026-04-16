#include "testing.h"

#include <string>
#include <vector>

#include "rhi-shared.h"

using namespace rhi;
using namespace rhi::testing;

// Helper: Write a pattern to heap allocation via shader
static void runInitPointerShader(IDevice* device, uint32_t val, DeviceAddress dst, uint32_t numElements)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-pointer-init", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    ComPtr<ICommandQueue> queue;
    REQUIRE_CALL(device->getQueue(QueueType::Graphics, queue.writeRef()));

    auto commandEncoder = queue->createCommandEncoder();
    auto passEncoder = commandEncoder->beginComputePass();
    auto rootObject = passEncoder->bindPipeline(pipeline);
    ShaderCursor shaderCursor(rootObject);
    shaderCursor["val"].setData(val);
    shaderCursor["dst"].setData(dst);
    passEncoder->dispatchCompute(numElements / 32, 1, 1);
    passEncoder->end();

    ComPtr<ICommandBuffer> cb = commandEncoder->finish();
    REQUIRE_CALL(queue->submit(cb));
}

// Helper: Copy from heap allocation to buffer via shader
static void runCopyPointerShader(IDevice* device, DeviceAddress src, DeviceAddress dst, uint32_t numElements)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-pointer-copy", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    ComPtr<ICommandQueue> queue;
    REQUIRE_CALL(device->getQueue(QueueType::Graphics, queue.writeRef()));

    auto commandEncoder = queue->createCommandEncoder();
    auto passEncoder = commandEncoder->beginComputePass();
    auto rootObject = passEncoder->bindPipeline(pipeline);
    ShaderCursor shaderCursor(rootObject);
    shaderCursor["src"].setData(src);
    shaderCursor["dst"].setData(dst);
    passEncoder->dispatchCompute(numElements / 32, 1, 1);
    passEncoder->end();

    ComPtr<ICommandBuffer> cb = commandEncoder->finish();
    REQUIRE_CALL(queue->submit(cb));
}

// Helper: Verify heap allocation contains expected pattern
// Reads back GPU memory and checks all elements match the expected value
static void verifyHeapPattern(IDevice* device, HeapAlloc& alloc, uint32_t expectedPattern, uint32_t numElements)
{
    // Create a readback buffer
    BufferDesc bufferDesc = {};
    bufferDesc.size = numElements * sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::CopyDestination;
    bufferDesc.memoryType = MemoryType::ReadBack;

    ComPtr<IBuffer> readbackBuffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, readbackBuffer.writeRef()));

    // Copy from heap allocation to buffer via shader
    runCopyPointerShader(device, alloc.getDeviceAddress(), readbackBuffer->getDeviceAddress(), numElements);

    // Wait for GPU
    ComPtr<ICommandQueue> queue;
    REQUIRE_CALL(device->getQueue(QueueType::Graphics, queue.writeRef()));
    queue->waitOnHost();

    // Read back and verify
    ComPtr<ISlangBlob> blob;
    REQUIRE_CALL(device->readBuffer(readbackBuffer, 0, bufferDesc.size, blob.writeRef()));
    auto data = (const uint32_t*)blob->getBufferPointer();

    for (uint32_t i = 0; i < numElements; i++)
    {
        if (data[i] != expectedPattern)
        {
            CAPTURE(i);
            CHECK_EQ(data[i], expectedPattern);
            break; // Don't spam on first failure
        }
    }
}

GPU_TEST_CASE("caching-allocator-enabled-by-default", CUDA)
{
    // Create a heap with default settings - caching should be enabled
    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    // Allocate and free memory
    HeapAllocDesc allocDesc;
    allocDesc.size = 1024 * 1024; // 1 MB
    allocDesc.alignment = 128;

    HeapAlloc allocation;
    REQUIRE_CALL(heap->allocate(allocDesc, &allocation));

    HeapReport report = heap->report();
    CHECK_EQ(report.numPages, 1);
    Size initialMemUsage = report.totalMemUsage;

    // Free the allocation
    REQUIRE_CALL(heap->free(allocation));

    // With caching enabled, the page should still exist (cached for reuse)
    report = heap->report();
    CHECK_EQ(report.totalAllocated, 0);
    CHECK_EQ(report.numAllocations, 0);
    // Page count should still be 1 (page is cached, not freed)
    CHECK_EQ(report.numPages, 1);
    CHECK_EQ(report.totalMemUsage, initialMemUsage);
}

GPU_TEST_CASE("caching-allocator-page-reuse", CUDA)
{
    // Test that freed pages are reused for new allocations
    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    HeapAllocDesc allocDesc;
    allocDesc.size = 1024 * 1024; // 1 MB
    allocDesc.alignment = 128;

    // First allocation
    HeapAlloc alloc1;
    REQUIRE_CALL(heap->allocate(allocDesc, &alloc1));

    HeapReport report = heap->report();
    CHECK_EQ(report.numPages, 1);
    Size pageSize = report.totalMemUsage;

    // Free the allocation
    REQUIRE_CALL(heap->free(alloc1));

    // Second allocation of same size - should reuse the cached page
    HeapAlloc alloc2;
    REQUIRE_CALL(heap->allocate(allocDesc, &alloc2));

    report = heap->report();
    // Should still have only 1 page (reused the cached one)
    CHECK_EQ(report.numPages, 1);
    CHECK_EQ(report.totalMemUsage, pageSize);

    // Clean up
    REQUIRE_CALL(heap->free(alloc2));
}

GPU_TEST_CASE("caching-allocator-multiple-pages", CUDA)
{
    // Test caching with multiple pages of different sizes
    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    std::vector<HeapAlloc> allocations;

    // Allocate pages of different sizes
    std::vector<Size> sizes = {
        512 * 1024,      // Small (8MB page)
        4 * 1024 * 1024, // Medium (8MB page)
        16 * 1024 * 1024 // Large (64MB page)
    };

    HeapAllocDesc allocDesc;
    allocDesc.alignment = 128;

    for (Size size : sizes)
    {
        allocDesc.size = size;
        HeapAlloc alloc;
        REQUIRE_CALL(heap->allocate(allocDesc, &alloc));
        allocations.push_back(alloc);
    }

    HeapReport report = heap->report();
    uint32_t initialPageCount = report.numPages;
    Size initialMemUsage = report.totalMemUsage;

    // Free all allocations
    for (auto& alloc : allocations)
    {
        REQUIRE_CALL(heap->free(alloc));
    }

    // Pages should be cached (not freed)
    report = heap->report();
    CHECK_EQ(report.totalAllocated, 0);
    CHECK_EQ(report.numAllocations, 0);
    CHECK_EQ(report.numPages, initialPageCount);
    CHECK_EQ(report.totalMemUsage, initialMemUsage);

    // Allocate again - should reuse cached pages
    allocations.clear();
    for (Size size : sizes)
    {
        allocDesc.size = size;
        HeapAlloc alloc;
        REQUIRE_CALL(heap->allocate(allocDesc, &alloc));
        allocations.push_back(alloc);
    }

    report = heap->report();
    // Page count should not have increased
    CHECK_EQ(report.numPages, initialPageCount);

    // Clean up
    for (auto& alloc : allocations)
    {
        REQUIRE_CALL(heap->free(alloc));
    }
}

GPU_TEST_CASE("caching-allocator-disabled", CUDA)
{
    // Test that caching can be disabled
    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;
    desc.caching.enabled = false;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    HeapAllocDesc allocDesc;
    allocDesc.size = 1024 * 1024;
    allocDesc.alignment = 128;

    // Allocate
    HeapAlloc alloc;
    REQUIRE_CALL(heap->allocate(allocDesc, &alloc));

    HeapReport report = heap->report();
    CHECK_EQ(report.numPages, 1);

    // Free
    REQUIRE_CALL(heap->free(alloc));

    // With caching disabled, page should be actually freed
    // (Note: freePage is called immediately, but removeEmptyPages needs to be called)
    REQUIRE_CALL(heap->removeEmptyPages());

    report = heap->report();
    CHECK_EQ(report.numPages, 0);
    CHECK_EQ(report.totalMemUsage, 0);
}


GPU_TEST_CASE("caching-allocator-stress-test", CUDA)
{
    // Stress test with 1 million allocations and frees.
    // Uses a rotating pool to maintain realistic memory pressure while
    // exercising the caching allocator at scale.

    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    // Use varying allocation sizes to stress different code paths
    const Size sizes[] = {64 * 1024, 128 * 1024, 256 * 1024, 512 * 1024};
    const int numSizes = sizeof(sizes) / sizeof(sizes[0]);

    HeapAllocDesc allocDesc;
    allocDesc.alignment = 128;

    // Rotating pool: maintain ~100 active allocations at any time
    constexpr int poolSize = 100;
    std::vector<HeapAlloc> pool(poolSize);
    std::vector<bool> poolActive(poolSize, false);

    // Track stats for verification
    uint64_t totalAllocations = 0;
    uint32_t maxPagesObserved = 0;

    // Perform 1 million allocation operations
    constexpr uint64_t totalOperations = 1000000;

    for (uint64_t op = 0; op < totalOperations; op++)
    {
        // Pick a random slot in the pool
        int slot = op % poolSize;

        // If slot is active, free it first
        if (poolActive[slot])
        {
            REQUIRE_CALL(heap->free(pool[slot]));
            poolActive[slot] = false;
        }

        // Allocate with varying sizes
        allocDesc.size = sizes[op % numSizes];
        REQUIRE_CALL(heap->allocate(allocDesc, &pool[slot]));
        poolActive[slot] = true;
        totalAllocations++;

        // Periodically check page count isn't exploding
        if (op % 100000 == 0)
        {
            HeapReport report = heap->report();
            if (report.numPages > maxPagesObserved)
            {
                maxPagesObserved = report.numPages;
            }
        }
    }

    // Clean up remaining allocations
    for (int i = 0; i < poolSize; i++)
    {
        if (poolActive[i])
        {
            REQUIRE_CALL(heap->free(pool[i]));
        }
    }

    // Verify caching worked correctly
    HeapReport report = heap->report();
    CHECK_EQ(report.totalAllocated, 0);
    CHECK_EQ(report.numAllocations, 0);

    // Key invariant: page count should be bounded regardless of allocation count.
    // With 100 concurrent allocations of up to 512KB each, we need at most ~50MB.
    // With 8MB default page size, that's ~7 pages max. Allow some headroom.
    CHECK(report.numPages <= 20);
    CHECK(maxPagesObserved <= 20);

    // Sanity check: we actually did 1 million allocations
    CHECK_EQ(totalAllocations, totalOperations);
}


GPU_TEST_CASE("caching-allocator-rapid-alloc-free", CUDA)
{
    // Test rapid allocation/free cycles that stress the caching system.
    // Verifies that pages are reused efficiently and don't grow unboundedly.

    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    HeapAllocDesc allocDesc;
    allocDesc.alignment = 128;

    // Simulate rapid alloc/free cycles with varying sizes
    for (int iteration = 0; iteration < 50; iteration++)
    {
        std::vector<HeapAlloc> tempAllocations;
        std::vector<Size> sizes = {64 * 1024, 256 * 1024, 1024 * 1024, 512 * 1024};

        for (Size size : sizes)
        {
            allocDesc.size = size;
            HeapAlloc alloc;
            REQUIRE_CALL(heap->allocate(allocDesc, &alloc));
            tempAllocations.push_back(alloc);
        }

        // Free all allocations
        for (auto& alloc : tempAllocations)
        {
            REQUIRE_CALL(heap->free(alloc));
        }
    }

    // Verify memory is being reused efficiently
    HeapReport report = heap->report();
    CHECK_EQ(report.totalAllocated, 0);
    CHECK_EQ(report.numAllocations, 0);

    // Pages should be cached for reuse
    CHECK(report.numPages > 0);
    // Should not have excessive pages (caching should be efficient)
    // 50 iterations * 4 allocations = 200 total, but pages should be reused
    CHECK(report.numPages <= 10);
}

GPU_TEST_CASE("caching-allocator-with-gpu-work", CUDA)
{
    // Test that caching works correctly when GPU actually uses the heap allocation.
    // Unlike basic caching tests, this verifies the interaction between
    // GPU work and the caching mechanism.

    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    // Size must be multiple of 32*sizeof(uint32_t) for shader
    constexpr Size allocSize = 32 * 1024; // 32 KB = 8192 uint32s
    constexpr uint32_t numElements = allocSize / sizeof(uint32_t);

    HeapAllocDesc allocDesc;
    allocDesc.size = allocSize;
    allocDesc.alignment = 128;

    // First allocation - write a pattern via GPU shader
    HeapAlloc alloc1;
    REQUIRE_CALL(heap->allocate(allocDesc, &alloc1));

    runInitPointerShader(device, 0xDEADBEEF, alloc1.getDeviceAddress(), numElements);
    // Verify the pattern was written correctly
    verifyHeapPattern(device, alloc1, 0xDEADBEEF, numElements);

    HeapReport report = heap->report();
    CHECK_EQ(report.numPages, 1);
    CHECK_EQ(report.numAllocations, 1);

    // Free the allocation - page should be cached
    REQUIRE_CALL(heap->free(alloc1));

    report = heap->report();
    CHECK_EQ(report.totalAllocated, 0);
    CHECK_EQ(report.numAllocations, 0);
    CHECK_EQ(report.numPages, 1); // Page cached

    // Second allocation - should reuse the cached page
    HeapAlloc alloc2;
    REQUIRE_CALL(heap->allocate(allocDesc, &alloc2));

    // Write a different pattern
    runInitPointerShader(device, 0xCAFEBABE, alloc2.getDeviceAddress(), numElements);
    // Verify second pattern
    verifyHeapPattern(device, alloc2, 0xCAFEBABE, numElements);

    report = heap->report();
    CHECK_EQ(report.numPages, 1); // Still 1 page (reused)

    REQUIRE_CALL(heap->free(alloc2));
}

GPU_TEST_CASE("caching-allocator-immediate-reuse-data-integrity", CUDA)
{
    // Test that CUDA same-stream immediate reuse doesn't corrupt data.
    // This is the key PyTorch-style optimization test:
    // - Allocate, use in shader, free, reallocate - memory may be reused
    // - CUDA FIFO ordering guarantees previous work completes before reuse
    // - We verify by writing different patterns in each iteration

    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    constexpr Size allocSize = 32 * 1024; // 32 KB
    constexpr uint32_t numElements = allocSize / sizeof(uint32_t);

    HeapAllocDesc allocDesc;
    allocDesc.size = allocSize;
    allocDesc.alignment = 128;

    // Run multiple iterations - each reuses memory from previous
    for (int iteration = 0; iteration < 20; iteration++)
    {
        HeapAlloc alloc;
        REQUIRE_CALL(heap->allocate(allocDesc, &alloc));

        // Write a unique pattern for this iteration
        uint32_t pattern = 0xABCD0000 | iteration;
        runInitPointerShader(device, pattern, alloc.getDeviceAddress(), numElements);

        // Verify BEFORE free to ensure CUDA FIFO ordering is working correctly.
        // If immediate reuse were unsafe, we'd see corruption from previous iteration.
        verifyHeapPattern(device, alloc, pattern, numElements);

        // Free immediately - with CUDA same-stream, this is safe
        REQUIRE_CALL(heap->free(alloc));
    }

    // Verify pages were reused (not growing)
    HeapReport report = heap->report();
    CHECK_EQ(report.totalAllocated, 0);
    CHECK_EQ(report.numAllocations, 0);
    // Should have only 1 page (all allocations fit in same page and were reused)
    CHECK_EQ(report.numPages, 1);
}

GPU_TEST_CASE("caching-allocator-concurrent-allocations-with-gpu", CUDA)
{
    // Test multiple concurrent allocations all used by GPU work.
    // Verifies that the caching system handles multiple in-flight allocations.

    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    constexpr Size allocSize = 32 * 1024; // 32 KB
    constexpr uint32_t numElements = allocSize / sizeof(uint32_t);

    HeapAllocDesc allocDesc;
    allocDesc.size = allocSize;
    allocDesc.alignment = 128;

    for (int cycle = 0; cycle < 5; cycle++)
    {
        // Allocate multiple blocks
        std::vector<HeapAlloc> allocations;
        for (int i = 0; i < 4; i++)
        {
            HeapAlloc alloc;
            REQUIRE_CALL(heap->allocate(allocDesc, &alloc));
            allocations.push_back(alloc);
        }

        // Use all allocations in GPU work
        for (size_t i = 0; i < allocations.size(); i++)
        {
            uint32_t pattern = (cycle << 16) | i;
            runInitPointerShader(device, pattern, allocations[i].getDeviceAddress(), numElements);
        }

        // Verify all patterns before freeing
        for (size_t i = 0; i < allocations.size(); i++)
        {
            uint32_t pattern = (cycle << 16) | i;
            verifyHeapPattern(device, allocations[i], pattern, numElements);
        }

        // Free all - with CUDA same-stream, immediate reuse is safe
        for (auto& alloc : allocations)
        {
            REQUIRE_CALL(heap->free(alloc));
        }
    }

    // Verify efficient page reuse
    HeapReport report = heap->report();
    CHECK_EQ(report.totalAllocated, 0);
    CHECK_EQ(report.numAllocations, 0);
    // 5 cycles * 4 allocations = 20 total, but pages should be reused
    CHECK(report.numPages <= 4);
}

GPU_TEST_CASE("caching-allocator-copy-between-heap-allocations", CUDA)
{
    // Test copying data between heap allocations using GPU.
    // Verifies that pointer-based shader access works correctly with caching.

    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    constexpr Size allocSize = 32 * 1024; // 32 KB
    constexpr uint32_t numElements = allocSize / sizeof(uint32_t);

    HeapAllocDesc allocDesc;
    allocDesc.size = allocSize;
    allocDesc.alignment = 128;

    // Allocate source and destination
    HeapAlloc srcAlloc, dstAlloc;
    REQUIRE_CALL(heap->allocate(allocDesc, &srcAlloc));
    REQUIRE_CALL(heap->allocate(allocDesc, &dstAlloc));

    // Initialize source with pattern
    runInitPointerShader(device, 0x12345678, srcAlloc.getDeviceAddress(), numElements);
    // Verify source pattern
    verifyHeapPattern(device, srcAlloc, 0x12345678, numElements);

    // Copy from source to destination
    runCopyPointerShader(device, srcAlloc.getDeviceAddress(), dstAlloc.getDeviceAddress(), numElements);
    // Verify the copy worked - destination should have source's pattern
    verifyHeapPattern(device, dstAlloc, 0x12345678, numElements);

    HeapReport report = heap->report();
    CHECK_EQ(report.numAllocations, 2);

    // Free both
    REQUIRE_CALL(heap->free(srcAlloc));
    REQUIRE_CALL(heap->free(dstAlloc));

    report = heap->report();
    CHECK_EQ(report.totalAllocated, 0);
    CHECK_EQ(report.numAllocations, 0);
    // Pages should be cached
    CHECK(report.numPages > 0);
}
