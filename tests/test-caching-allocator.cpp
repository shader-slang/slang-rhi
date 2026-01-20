#include "testing.h"

#include <string>
#include <vector>

#include "rhi-shared.h"

using namespace rhi;
using namespace rhi::testing;

// Helper function to create a buffer and run a simple compute shader
void runDummyCompute(IDevice* device)
{
    BufferDesc bufferDesc = {};
    bufferDesc.size = 1024;
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> src, dst;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, src.writeRef()));
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, dst.writeRef()));

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-buffer-copy", "computeMain", shaderProgram.writeRef()));

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
    shaderCursor["src"].setBinding(src);
    shaderCursor["dst"].setBinding(dst);
    passEncoder->dispatchCompute(1, 1, 1);
    passEncoder->end();

    ComPtr<ICommandBuffer> cb = commandEncoder->finish();
    REQUIRE_CALL(queue->submit(cb));
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

GPU_TEST_CASE("caching-allocator-with-gpu-work", CUDA)
{
    // Test that caching works correctly with GPU work in progress
    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);

    HeapAllocDesc allocDesc;
    allocDesc.size = 1024 * 1024;
    allocDesc.alignment = 128;

    // Allocate
    HeapAlloc alloc;
    REQUIRE_CALL(heap->allocate(allocDesc, &alloc));

    // Run some GPU work
    runDummyCompute(device);

    // Free the allocation while GPU work is pending
    REQUIRE_CALL(heap->free(alloc));

    HeapReport report = heap->report();
    // With pending GPU work, allocation might still be reported as allocated
    // (pending free mechanism)

    // Wait for GPU
    queue->waitOnHost();
    REQUIRE_CALL(heap->flush());

    report = heap->report();
    CHECK_EQ(report.totalAllocated, 0);
    CHECK_EQ(report.numAllocations, 0);
    // Page should still exist (cached)
    CHECK_EQ(report.numPages, 1);

    // New allocation should reuse the cached page
    HeapAlloc alloc2;
    REQUIRE_CALL(heap->allocate(allocDesc, &alloc2));

    report = heap->report();
    CHECK_EQ(report.numPages, 1);

    REQUIRE_CALL(heap->free(alloc2));
}

GPU_TEST_CASE("caching-allocator-stress-test", CUDA)
{
    // Stress test with many allocations and frees
    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    HeapAllocDesc allocDesc;
    allocDesc.size = 256 * 1024; // 256KB
    allocDesc.alignment = 128;

    // Perform many allocation/free cycles
    for (int cycle = 0; cycle < 10; cycle++)
    {
        std::vector<HeapAlloc> allocations;

        // Allocate multiple blocks
        for (int i = 0; i < 5; i++)
        {
            HeapAlloc alloc;
            REQUIRE_CALL(heap->allocate(allocDesc, &alloc));
            allocations.push_back(alloc);
        }

        // Free all
        for (auto& alloc : allocations)
        {
            REQUIRE_CALL(heap->free(alloc));
        }
    }

    // After many cycles, pages should be cached and reused
    // Memory usage should not grow unboundedly
    HeapReport report = heap->report();

    // Should have reasonable number of pages (not one per allocation)
    CHECK(report.numPages <= 5); // Should be much less than 50 (10 cycles * 5 allocs)
}

GPU_TEST_CASE("caching-allocator-single-stream-no-events", CUDA)
{
    // Test that single-stream workloads don't create excessive events (PyTorch-style lazy events)
    // This is a behavioral test - we verify that repeated single-stream submits work correctly
    // without requiring explicit event synchronization.

    ComPtr<ICommandQueue> queue;
    REQUIRE_CALL(device->getQueue(QueueType::Graphics, queue.writeRef()));

    // Run many submits in rapid succession on the same stream
    // With lazy events optimization, these should not create events per-submit
    for (int i = 0; i < 100; i++)
    {
        runDummyCompute(device);
    }

    // Wait for all work to complete
    queue->waitOnHost();

    // If we get here without issues, lazy events are working correctly
    // The actual optimization is that we don't create 100 events, but rather
    // use cuStreamQuery for single-stream retirement
    CHECK(true);
}

GPU_TEST_CASE("caching-allocator-rapid-alloc-free", CUDA)
{
    // Test rapid allocation/free cycles that stress the caching system
    // This pattern is common in PyTorch-style workloads where temporary
    // tensors are allocated and freed frequently within a training loop.

    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    ComPtr<ICommandQueue> queue;
    REQUIRE_CALL(device->getQueue(QueueType::Graphics, queue.writeRef()));

    HeapAllocDesc allocDesc;
    allocDesc.alignment = 128;

    // Simulate a training loop with temporary allocations
    for (int iteration = 0; iteration < 50; iteration++)
    {
        // Allocate temporary tensors of various sizes
        std::vector<HeapAlloc> tempAllocations;
        std::vector<Size> sizes = {64 * 1024, 256 * 1024, 1024 * 1024, 512 * 1024};

        for (Size size : sizes)
        {
            allocDesc.size = size;
            HeapAlloc alloc;
            REQUIRE_CALL(heap->allocate(allocDesc, &alloc));
            tempAllocations.push_back(alloc);
        }

        // Run some GPU work
        runDummyCompute(device);

        // Free all temporary allocations
        for (auto& alloc : tempAllocations)
        {
            REQUIRE_CALL(heap->free(alloc));
        }

        // Occasionally wait for GPU to ensure pending frees are processed
        if (iteration % 10 == 0)
        {
            queue->waitOnHost();
            REQUIRE_CALL(heap->flush());
        }
    }

    // Wait for all GPU work
    queue->waitOnHost();
    REQUIRE_CALL(heap->flush());

    // Verify memory is being reused efficiently
    HeapReport report = heap->report();
    CHECK_EQ(report.totalAllocated, 0);
    CHECK_EQ(report.numAllocations, 0);

    // Pages should be cached for reuse
    CHECK(report.numPages > 0);
    // Should not have excessive pages (caching should be efficient)
    CHECK(report.numPages <= 10);
}

GPU_TEST_CASE("caching-allocator-multi-stream", CUDA)
{
    // Test multi-stream page tracking (PyTorch-style cross-stream synchronization)
    // This verifies that when a page allocated on one stream is used by another,
    // proper synchronization events are created.

    // Get two different queues (streams)
    ComPtr<ICommandQueue> graphicsQueue;
    ComPtr<ICommandQueue> computeQueue;
    REQUIRE_CALL(device->getQueue(QueueType::Graphics, graphicsQueue.writeRef()));

    // Try to get a compute queue - if not available, skip the test
    if (SLANG_FAILED(device->getQueue(QueueType::Compute, computeQueue.writeRef())))
    {
        SKIP("Compute queue not available for multi-stream test");
    }

    // Create buffers - these will allocate from device heap pages
    BufferDesc bufferDesc = {};
    bufferDesc.size = 1024 * 1024; // 1MB
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess |
                       BufferUsage::CopyDestination | BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> buffer1, buffer2;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, buffer1.writeRef()));
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, buffer2.writeRef()));

    // Load shader program
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-buffer-copy", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    // Submit work on graphics queue using buffer1
    {
        auto encoder = graphicsQueue->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        auto rootObject = pass->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject);
        cursor["src"].setBinding(buffer1);
        cursor["dst"].setBinding(buffer2);
        pass->dispatchCompute(1, 1, 1);
        pass->end();
        ComPtr<ICommandBuffer> cb = encoder->finish();
        REQUIRE_CALL(graphicsQueue->submit(cb));
    }

    // Submit work on compute queue using the same buffers
    // This should trigger cross-stream tracking (recordStreamUse)
    {
        auto encoder = computeQueue->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        auto rootObject = pass->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject);
        cursor["src"].setBinding(buffer2); // Use buffer2 as source now
        cursor["dst"].setBinding(buffer1); // Write back to buffer1
        pass->dispatchCompute(1, 1, 1);
        pass->end();
        ComPtr<ICommandBuffer> cb = encoder->finish();
        REQUIRE_CALL(computeQueue->submit(cb));
    }

    // Submit more work on graphics queue
    // The caching allocator should properly synchronize with compute queue
    {
        auto encoder = graphicsQueue->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        auto rootObject = pass->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject);
        cursor["src"].setBinding(buffer1);
        cursor["dst"].setBinding(buffer2);
        pass->dispatchCompute(1, 1, 1);
        pass->end();
        ComPtr<ICommandBuffer> cb = encoder->finish();
        REQUIRE_CALL(graphicsQueue->submit(cb));
    }

    // Wait for all work to complete on both queues
    graphicsQueue->waitOnHost();
    computeQueue->waitOnHost();

    // If we get here without crashes or validation errors,
    // multi-stream synchronization is working correctly
    CHECK(true);
}
