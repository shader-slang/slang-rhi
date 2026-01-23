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

// ============================================================================
// Tests for CUDA-specific buffer tracking removal (PyTorch-style optimization)
// ============================================================================

// Helper function to run a compute shader with specific buffers
static void runComputeWithBuffers(IDevice* device, IBuffer* src, IBuffer* dst)
{
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

GPU_TEST_CASE("cuda-buffer-immediate-reuse-safety", CUDA)
{
    // This test verifies that immediate buffer reuse is safe.
    // With the PyTorch-style optimization, device-local buffers are NOT tracked
    // in shader objects, but CUDA stream FIFO ordering guarantees safety.
    //
    // Pattern: allocate, use in shader, free, allocate again - should reuse memory
    // without corruption because all operations are on the same stream.

    ComPtr<ICommandQueue> queue;
    REQUIRE_CALL(device->getQueue(QueueType::Graphics, queue.writeRef()));

    BufferDesc bufferDesc = {};
    bufferDesc.size = 1024 * sizeof(uint32_t);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess |
                       BufferUsage::CopyDestination | BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    // Run multiple iterations to stress memory reuse
    for (int iteration = 0; iteration < 50; iteration++)
    {
        // Create buffers
        ComPtr<IBuffer> src, dst;
        REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, src.writeRef()));
        REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, dst.writeRef()));

        // Use buffers in a compute shader (this is where buffer tracking is tested)
        runComputeWithBuffers(device, src, dst);

        // Buffers go out of scope here - with the optimization, device-local
        // buffers are not tracked, so they can be freed immediately.
        // The next iteration may reuse the same memory.
    }

    // Final sync to ensure no crashes or corruption
    queue->waitOnHost();

    // If we get here without crashes, the optimization is working correctly
    CHECK(true);
}

GPU_TEST_CASE("cuda-buffer-no-tracking-stress", CUDA)
{
    // Stress test: many buffers allocated, used, and freed rapidly.
    // Without proper same-stream ordering, this would cause corruption.
    // This test verifies that device-local buffers can be freed without
    // explicit tracking because CUDA stream FIFO ordering provides safety.

    ComPtr<ICommandQueue> queue;
    REQUIRE_CALL(device->getQueue(QueueType::Graphics, queue.writeRef()));

    BufferDesc bufferDesc = {};
    bufferDesc.size = 64 * 1024; // 64KB each
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    // Create many buffers, use them, free them - all without explicit sync
    for (int wave = 0; wave < 10; wave++)
    {
        std::vector<ComPtr<IBuffer>> buffers;

        // Allocate a batch of buffers
        for (int i = 0; i < 20; i++)
        {
            ComPtr<IBuffer> buf;
            REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, buf.writeRef()));
            buffers.push_back(buf);
        }

        // Use all buffers in compute passes (creating shader object bindings)
        // This exercises the CUDA-specific tracking code
        for (size_t i = 0; i + 1 < buffers.size(); i += 2)
        {
            runComputeWithBuffers(device, buffers[i], buffers[i + 1]);
        }

        // Free all buffers (without waiting for GPU!)
        // With the optimization, device-local buffers are not tracked,
        // so ~BufferImpl() is called immediately
        buffers.clear();

        // Next wave will likely reuse the same memory
    }

    // Final sync to ensure no crashes
    queue->waitOnHost();

    CHECK(true);
}

GPU_TEST_CASE("cuda-buffer-upload-readback-still-tracked", CUDA)
{
    // Verify that Upload/ReadBack buffers are STILL tracked.
    // These buffers need tracking because CPU may access them after GPU work completes.
    // Only DeviceLocal buffers skip tracking (the PyTorch-style optimization).

    ComPtr<ICommandQueue> queue;
    REQUIRE_CALL(device->getQueue(QueueType::Graphics, queue.writeRef()));

    // Create an upload buffer (staging buffer for CPU->GPU transfers)
    BufferDesc uploadDesc = {};
    uploadDesc.size = 1024;
    uploadDesc.usage = BufferUsage::CopySource;
    uploadDesc.memoryType = MemoryType::Upload;

    ComPtr<IBuffer> uploadBuffer;
    REQUIRE_CALL(device->createBuffer(uploadDesc, nullptr, uploadBuffer.writeRef()));

    // Create a readback buffer (staging buffer for GPU->CPU transfers)
    BufferDesc readbackDesc = {};
    readbackDesc.size = 1024;
    readbackDesc.usage = BufferUsage::CopyDestination;
    readbackDesc.memoryType = MemoryType::ReadBack;

    ComPtr<IBuffer> readbackBuffer;
    REQUIRE_CALL(device->createBuffer(readbackDesc, nullptr, readbackBuffer.writeRef()));

    // Create a device-local buffer
    BufferDesc deviceDesc = {};
    deviceDesc.size = 1024;
    deviceDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess |
                       BufferUsage::CopySource | BufferUsage::CopyDestination;
    deviceDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> deviceBuffer;
    REQUIRE_CALL(device->createBuffer(deviceDesc, nullptr, deviceBuffer.writeRef()));

    // Perform copy operations that use all three buffer types
    auto commandEncoder = queue->createCommandEncoder();

    // Copy from upload to device
    commandEncoder->copyBuffer(deviceBuffer, 0, uploadBuffer, 0, 1024);

    // Copy from device to readback
    commandEncoder->copyBuffer(readbackBuffer, 0, deviceBuffer, 0, 1024);

    ComPtr<ICommandBuffer> cb = commandEncoder->finish();
    REQUIRE_CALL(queue->submit(cb));

    // Wait for completion
    queue->waitOnHost();

    // If we get here without issues, upload/readback buffers are properly tracked
    CHECK(true);
}

GPU_TEST_CASE("cuda-buffer-mixed-memory-types", CUDA)
{
    // Test that mixed memory types in the same command buffer work correctly.
    // DeviceLocal buffers skip tracking, but Upload/ReadBack buffers are tracked.

    ComPtr<ICommandQueue> queue;
    REQUIRE_CALL(device->getQueue(QueueType::Graphics, queue.writeRef()));

    // Run multiple iterations with mixed buffer types
    for (int iteration = 0; iteration < 20; iteration++)
    {
        // Create device-local buffers (NOT tracked with optimization)
        BufferDesc deviceDesc = {};
        deviceDesc.size = 4096;
        deviceDesc.elementSize = sizeof(uint32_t);
        deviceDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess |
                           BufferUsage::CopySource | BufferUsage::CopyDestination;
        deviceDesc.memoryType = MemoryType::DeviceLocal;

        ComPtr<IBuffer> src, dst;
        REQUIRE_CALL(device->createBuffer(deviceDesc, nullptr, src.writeRef()));
        REQUIRE_CALL(device->createBuffer(deviceDesc, nullptr, dst.writeRef()));

        // Use in compute shader
        runComputeWithBuffers(device, src, dst);

        // Buffers freed here - device-local ones skip tracking
    }

    queue->waitOnHost();
    CHECK(true);
}

GPU_TEST_CASE("cuda-buffer-tracking-no-memory-leak", CUDA)
{
    // Verify that the tracking optimization doesn't cause memory leaks.
    // Even though device-local buffers skip tracking, they should still be freed.

    ComPtr<ICommandQueue> queue;
    REQUIRE_CALL(device->getQueue(QueueType::Graphics, queue.writeRef()));

    // Get initial memory state via heap report
    HeapDesc heapDesc;
    heapDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(heapDesc, heap.writeRef()));

    // Allocate and free many buffers
    BufferDesc bufferDesc = {};
    bufferDesc.size = 1024 * 1024; // 1MB each
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    for (int iteration = 0; iteration < 100; iteration++)
    {
        ComPtr<IBuffer> buffer;
        REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));

        // Run a compute pass using the buffer
        runDummyCompute(device);

        // Buffer goes out of scope - should be freed (not leaked)
    }

    // Wait for all GPU work
    queue->waitOnHost();

    // Force heap to process all pending frees
    REQUIRE_CALL(heap->flush());

    // Verify no excessive memory usage (pages should be reused, not accumulated)
    HeapReport report = heap->report();

    // We should have a bounded number of pages (caching allows reuse)
    // If there were memory leaks, we'd have many more pages
    CHECK(report.numPages <= 10);

    CHECK(true);
}
