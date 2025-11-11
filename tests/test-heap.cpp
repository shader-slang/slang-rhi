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

void runCopyBufferShader(IDevice* device, IBuffer* src, IBuffer* dst)
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
    passEncoder->dispatchCompute(src->getDesc().size / (src->getDesc().elementSize * 32), 1, 1);
    passEncoder->end();

    ComPtr<ICommandBuffer> cb = commandEncoder->finish();
    REQUIRE_CALL(queue->submit(cb));
}

void runInitPointerShader(IDevice* device, uint32_t val, DeviceAddress dst, uint32_t numElements)
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

void runCopyPointerShader(IDevice* device, DeviceAddress src, DeviceAddress dst, uint32_t numElements)
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

ComPtr<IBuffer> createBuffer(IDevice* device, uint32_t size)
{
    // Setup buffer descriptor
    BufferDesc bufferDesc = {};
    bufferDesc.size = size;
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));
    return buffer;
}

GPU_TEST_CASE("heap-create", CUDA | Vulkan)
{
    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));
}

GPU_TEST_CASE("heap-allocate", CUDA | Vulkan)
{
    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    HeapAllocDesc allocDesc;
    allocDesc.size = 1024 * 1024; // 1 MB
    allocDesc.alignment = 128;

    HeapAlloc allocation;
    REQUIRE_CALL(heap->allocate(allocDesc, &allocation));
    CHECK_EQ(allocation.size, allocDesc.size);

    HeapReport report = heap->report();
    CHECK_EQ(report.totalAllocated, allocDesc.size);
    CHECK_EQ(report.numAllocations, 1);
    CHECK_EQ(report.totalMemUsage, 8 * 1024 * 1024); // assume 1 small page of 8 MB
    CHECK_EQ(report.numPages, 1);

    REQUIRE_CALL(heap->free(allocation));

    report = heap->report();
    CHECK_EQ(report.totalAllocated, 0);
    CHECK_EQ(report.numAllocations, 0);
    CHECK_EQ(report.totalMemUsage, 8 * 1024 * 1024); // assume 1 small page of 8 MB
    CHECK_EQ(report.numPages, 1);

    REQUIRE_CALL(heap->removeEmptyPages());

    report = heap->report();
    CHECK_EQ(report.totalAllocated, 0);
    CHECK_EQ(report.numAllocations, 0);
    CHECK_EQ(report.totalMemUsage, 0);
    CHECK_EQ(report.numPages, 0);
}

GPU_TEST_CASE("heap-submit", CUDA | Vulkan)
{
    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    HeapAllocDesc allocDesc;
    allocDesc.size = 1024 * 1024; // 1 MB
    allocDesc.alignment = 128;    // 256 KB

    HeapAlloc allocation;
    REQUIRE_CALL(heap->allocate(allocDesc, &allocation));
    CHECK_EQ(allocation.size, allocDesc.size);

    HeapReport report = heap->report();
    CHECK_EQ(report.totalAllocated, allocDesc.size);
    CHECK_EQ(report.numAllocations, 1);
    CHECK_EQ(report.totalMemUsage, 8 * 1024 * 1024); // assume 1 small page of 8 MB
    CHECK_EQ(report.numPages, 1);

    // Run dummy shader just to create a submit
    auto src = createBuffer(device, 1024);
    auto dst = createBuffer(device, 1024);
    runCopyBufferShader(device, src.get(), dst.get());

    // Request a free, which should not actually trigger
    // yet as the latest submit hasn't completed
    REQUIRE_CALL(heap->free(allocation));
    report = heap->report();
    CHECK_EQ(report.totalAllocated, allocDesc.size);
    CHECK_EQ(report.numAllocations, 1);
    CHECK_EQ(report.totalMemUsage, 8 * 1024 * 1024); // assume 1 small page of 8 MB
    CHECK_EQ(report.numPages, 1);

    // Wait for the queue to complete
    device->getQueue(QueueType::Graphics)->waitOnHost();

    // Flush the heap (TODO: Remove once hooked into device logic)
    REQUIRE_CALL(heap->flush());

    // Now the free should be processed
    report = heap->report();
    CHECK_EQ(report.totalAllocated, 0);
    CHECK_EQ(report.numAllocations, 0);
    CHECK_EQ(report.totalMemUsage, 8 * 1024 * 1024); // assume 1 small page of 8 MB
    CHECK_EQ(report.numPages, 1);

    REQUIRE_CALL(heap->removeEmptyPages());

    report = heap->report();
    CHECK_EQ(report.totalAllocated, 0);
    CHECK_EQ(report.numAllocations, 0);
    CHECK_EQ(report.totalMemUsage, 0);
    CHECK_EQ(report.numPages, 0);
}

struct AllocationInfo
{
    ComPtr<IBuffer> buffer;
    uint32_t pattern;
};

GPU_TEST_CASE("heap-pointer-stress-test", CUDA | Vulkan)
{
    ComputePipelineDesc pipelineDesc = {};

    ComPtr<IShaderProgram> initPtrShaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-pointer-init", "computeMain", initPtrShaderProgram.writeRef()));

    pipelineDesc.program = initPtrShaderProgram.get();
    ComPtr<IComputePipeline> initPtrpipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, initPtrpipeline.writeRef()));

    ComPtr<IShaderProgram> copyPtrShaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-pointer-copy", "computeMain", copyPtrShaderProgram.writeRef()));
    pipelineDesc.program = copyPtrShaderProgram.get();
    ComPtr<IComputePipeline> copyPtrpipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, copyPtrpipeline.writeRef()));


    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    std::vector<AllocationInfo> allocations;

    auto queue = device->getQueue(QueueType::Graphics);

    // Up front allocate a load of buffers
    for (int i = 0; i < 10; i++)
    {
        ComPtr<IBuffer> dst;
        BufferDesc bufferDesc = {};
        bufferDesc.size = 4 * 1024 * 1024;
        bufferDesc.format = Format::Undefined;
        bufferDesc.elementSize = sizeof(uint32_t);
        bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                           BufferUsage::CopySource;
        bufferDesc.defaultState = ResourceState::UnorderedAccess;
        bufferDesc.memoryType = MemoryType::DeviceLocal;
        device->createBuffer(bufferDesc, nullptr, dst.writeRef());
        allocations.push_back(AllocationInfo{dst, (uint32_t)i + 1});
    }

    // Run a load of compute operations that use temp allocations and generate a lot of GPU
    // work so the queues get busy. This should result in the heap having to delay
    // freeing of the temp buffers until the GPU is finished with them.
    for (auto& alloc : allocations)
    {
        HeapAlloc src;
        HeapAllocDesc allocDesc;
        allocDesc.size = alloc.buffer->getDesc().size;
        allocDesc.alignment = 128;
        REQUIRE_CALL(heap->allocate(allocDesc, &src));

        auto commandEncoder = queue->createCommandEncoder();
        {
            auto passEncoder = commandEncoder->beginComputePass();
            auto rootObject = passEncoder->bindPipeline(initPtrpipeline);
            ShaderCursor shaderCursor(rootObject);
            shaderCursor["val"].setData(alloc.pattern);
            shaderCursor["dst"].setData(src.getDeviceAddress());
            for (int d = 0; d < 100; d++)
                passEncoder->dispatchCompute(alloc.buffer->getDesc().size / (4 * 32), 1, 1);
            passEncoder->end();
        }
        commandEncoder->globalBarrier();
        {
            auto passEncoder = commandEncoder->beginComputePass();
            auto rootObject = passEncoder->bindPipeline(copyPtrpipeline);
            ShaderCursor shaderCursor(rootObject);
            shaderCursor["src"].setData(src.getDeviceAddress());
            shaderCursor["dst"].setData(alloc.buffer->getDeviceAddress());
            for (int d = 0; d < 100; d++)
                passEncoder->dispatchCompute(alloc.buffer->getDesc().size / (4 * 32), 1, 1);
            passEncoder->end();
        }

        ComPtr<ICommandBuffer> cb = commandEncoder->finish();
        REQUIRE_CALL(queue->submit(cb));

        // Free src
        REQUIRE_CALL(heap->free(src));
        heap->flush();
    }

    // Check contents of buffers
    for (auto& alloc : allocations)
    {
        ComPtr<ISlangBlob> blob;
        device->readBuffer(alloc.buffer, 0, alloc.buffer->getDesc().size, blob.writeRef());
        auto data = (uint32_t*)blob->getBufferPointer();
        for (size_t i = 0; i < alloc.buffer->getDesc().size / sizeof(uint32_t); i++)
        {
            if (data[i] != alloc.pattern)
            {
                printf("Data mismatch at %d, expected %d, got %d\n", (int)i, alloc.pattern, data[i]);
            }
            CHECK_EQ(data[i], alloc.pattern);
            if (data[i] != alloc.pattern)
                break;
        }
    }
}

// Helper function to check if two allocations overlap
bool allocationsOverlap(const HeapAlloc& a, const HeapAlloc& b)
{
    uint64_t aStart = a.getDeviceAddress();
    uint64_t aEnd = aStart + a.size;
    uint64_t bStart = b.getDeviceAddress();
    uint64_t bEnd = bStart + b.size;

    return !(aEnd <= bStart || bEnd <= aStart);
}

GPU_TEST_CASE("heap-no-overlaps", CUDA | Vulkan)
{
    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    std::vector<HeapAlloc> allocations;

    // Create multiple allocations of various sizes
    std::vector<size_t> sizes = {1024, 4096, 16384, 65536, 262144, 1048576};

    // Use platform-appropriate alignment
    size_t alignment = 128;

    for (size_t size : sizes)
    {
        HeapAllocDesc allocDesc;
        allocDesc.size = size;
        allocDesc.alignment = alignment;

        HeapAlloc allocation;
        REQUIRE_CALL(heap->allocate(allocDesc, &allocation));
        allocations.push_back(allocation);
    }

    // Verify no allocations overlap
    for (size_t i = 0; i < allocations.size(); i++)
    {
        for (size_t j = i + 1; j < allocations.size(); j++)
        {
            CHECK(!allocationsOverlap(allocations[i], allocations[j]));
        }
    }

    // Clean up
    for (auto& alloc : allocations)
    {
        REQUIRE_CALL(heap->free(alloc));
    }
}

GPU_TEST_CASE("heap-alloc-free-no-overlaps", CUDA | Vulkan)
{
    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    std::vector<HeapAlloc> currentAllocations;

    // Do a series of allocations and frees
    for (int iteration = 0; iteration < 3; iteration++)
    {
        // Allocate some memory
        for (int i = 0; i < 5; i++)
        {
            HeapAllocDesc allocDesc;
            allocDesc.size = (i + 1) * 8192; // Varying sizes
            allocDesc.alignment = 128;

            HeapAlloc allocation;
            REQUIRE_CALL(heap->allocate(allocDesc, &allocation));
            currentAllocations.push_back(allocation);
        }

        // Free some allocations (not all)
        if (currentAllocations.size() >= 3)
        {
            for (int i = 0; i < 2; i++)
            {
                REQUIRE_CALL(heap->free(currentAllocations.back()));
                currentAllocations.pop_back();
            }
        }
    }

    // Verify remaining allocations don't overlap
    for (size_t i = 0; i < currentAllocations.size(); i++)
    {
        for (size_t j = i + 1; j < currentAllocations.size(); j++)
        {
            CHECK(!allocationsOverlap(currentAllocations[i], currentAllocations[j]));
        }
    }

    // Clean up remaining allocations
    for (auto& alloc : currentAllocations)
    {
        REQUIRE_CALL(heap->free(alloc));
    }
}

GPU_TEST_CASE("heap-alignment-sizes", CUDA | Vulkan)
{
    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    std::vector<HeapAlloc> allocations;

    // Test various alignment and size combinations
    struct TestCase
    {
        size_t size;
        size_t alignment;
    };

    // Use platform-appropriate alignments
    std::vector<TestCase> testCases;
    testCases = {
        {1023, 64},     // Size not aligned to alignment
        {1024, 128},    // Size multiple of alignment
        {4096, 64},     // Size multiple of alignment
        {65535, 128},   // Odd size with large alignment
        {262144, 128},  // Large size with large alignment
        {1, 64},        // Minimal size
        {1048576, 128}, // Large size with standard alignment
    };

    for (auto& testCase : testCases)
    {
        HeapAllocDesc allocDesc;
        allocDesc.size = testCase.size;
        allocDesc.alignment = testCase.alignment;

        HeapAlloc allocation;
        REQUIRE_CALL(heap->allocate(allocDesc, &allocation));

        // Verify the allocation respects alignment
        CHECK_EQ(allocation.getDeviceAddress() % testCase.alignment, 0);

        // Verify the allocation size is at least what was requested
        CHECK(allocation.size >= testCase.size);

        allocations.push_back(allocation);
    }

    // Verify no overlaps with different alignments/sizes
    for (size_t i = 0; i < allocations.size(); i++)
    {
        for (size_t j = i + 1; j < allocations.size(); j++)
        {
            CHECK(!allocationsOverlap(allocations[i], allocations[j]));
        }
    }

    // Clean up
    for (auto& alloc : allocations)
    {
        REQUIRE_CALL(heap->free(alloc));
    }
}

GPU_TEST_CASE("heap-multiple-submits-pending-frees", CUDA | Vulkan)
{
    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);

    std::vector<HeapAlloc> pendingFrees;

    // Create multiple submits with allocations that will be freed
    for (int submitIndex = 0; submitIndex < 5; submitIndex++)
    {
        // Allocate some memory for this submit
        HeapAllocDesc allocDesc;
        allocDesc.size = (submitIndex + 1) * 65536; // Varying sizes
        allocDesc.alignment = 128;

        HeapAlloc allocation;
        REQUIRE_CALL(heap->allocate(allocDesc, &allocation));

        // Create some GPU work
        auto src = createBuffer(device, 1024);
        auto dst = createBuffer(device, 1024);
        runCopyBufferShader(device, src.get(), dst.get());

        // Queue the allocation for freeing - this should create pending frees
        // since we haven't waited for GPU completion
        REQUIRE_CALL(heap->free(allocation));
        pendingFrees.push_back(allocation);

        // Don't wait - let submits pile up
    }

    // At this point, we should have multiple pending frees
    HeapReport report = heap->report();

    // The allocations should still be counted as allocated since GPU work is pending
    CHECK(report.totalAllocated > 0);
    CHECK(report.numAllocations > 0);

    // Now wait for all GPU work to complete
    queue->waitOnHost();

    // Flush the heap to process pending frees
    REQUIRE_CALL(heap->flush());

    // Now all allocations should be freed
    report = heap->report();
    CHECK_EQ(report.totalAllocated, 0);
    CHECK_EQ(report.numAllocations, 0);

    // Create new allocations to verify the heap is still functional
    // and that freed memory can be reused
    std::vector<HeapAlloc> newAllocations;
    for (int i = 0; i < 3; i++)
    {
        HeapAllocDesc allocDesc;
        allocDesc.size = 32768;
        allocDesc.alignment = 128;

        HeapAlloc allocation;
        REQUIRE_CALL(heap->allocate(allocDesc, &allocation));
        newAllocations.push_back(allocation);
    }

    // Verify new allocations don't overlap
    for (size_t i = 0; i < newAllocations.size(); i++)
    {
        for (size_t j = i + 1; j < newAllocations.size(); j++)
        {
            CHECK(!allocationsOverlap(newAllocations[i], newAllocations[j]));
        }
    }

    // Clean up
    for (auto& alloc : newAllocations)
    {
        REQUIRE_CALL(heap->free(alloc));
    }
}

GPU_TEST_CASE("heap-fragmentation-test", CUDA | Vulkan)
{
    HeapDesc desc;
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    // Create a pattern that could lead to fragmentation
    std::vector<HeapAlloc> allocations;

    // Allocate 10 blocks
    for (int i = 0; i < 10; i++)
    {
        HeapAllocDesc allocDesc;
        allocDesc.size = 65536; // 64KB each
        allocDesc.alignment = 128;

        HeapAlloc allocation;
        REQUIRE_CALL(heap->allocate(allocDesc, &allocation));
        allocations.push_back(allocation);
    }

    // Free every other block to create fragmentation
    for (int i = 1; i < 10; i += 2)
    {
        REQUIRE_CALL(heap->free(allocations[i]));
    }

    // Try to allocate a larger block that might not fit in the holes
    HeapAllocDesc largeAllocDesc;
    largeAllocDesc.size = 131072; // 128KB - larger than the 64KB holes
    largeAllocDesc.alignment = 128;

    HeapAlloc largeAllocation;
    REQUIRE_CALL(heap->allocate(largeAllocDesc, &largeAllocation));

    // Verify the large allocation doesn't overlap with remaining allocations
    for (int i = 0; i < 10; i += 2) // Only check non-freed allocations
    {
        CHECK(!allocationsOverlap(allocations[i], largeAllocation));
    }

    // Clean up remaining allocations
    for (int i = 0; i < 10; i += 2)
    {
        REQUIRE_CALL(heap->free(allocations[i]));
    }
    REQUIRE_CALL(heap->free(largeAllocation));
}

GPU_TEST_CASE("heap-reports", ALL)
{
    auto deviceType = device->getDeviceType();

    // First, query the number of heaps
    uint32_t heapCount = 0;
    REQUIRE_CALL(device->reportHeaps(nullptr, &heapCount));

    if (deviceType == DeviceType::CUDA)
    {
        // CUDA should report 2 heaps (device and host memory)
        CHECK(heapCount == 2);

        // Test with exact buffer size
        std::vector<HeapReport> heapReports(heapCount);
        uint32_t actualCount = heapCount;
        REQUIRE_CALL(device->reportHeaps(heapReports.data(), &actualCount));
        CHECK(actualCount == heapCount);

        // Check that heap labels are set
        CHECK(strlen(heapReports[0].label) > 0);
        CHECK(strlen(heapReports[1].label) > 0);

        // Test with buffer that's too small - should return error
        HeapReport singleHeap;
        uint32_t limitedCount = 1;
        Result result = device->reportHeaps(&singleHeap, &limitedCount);
        CHECK(result == SLANG_E_BUFFER_TOO_SMALL);
    }
    else
    {
        // Other devices should report no heaps (default implementation)
        CHECK(heapCount == 0);
    }
}
