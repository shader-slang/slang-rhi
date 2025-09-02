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
    slang::ProgramLayout* slangReflection = nullptr;
    REQUIRE_CALL(loadComputeProgram(device, shaderProgram, "test-buffer-copy", "computeMain", slangReflection));

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
    slang::ProgramLayout* slangReflection = nullptr;
    REQUIRE_CALL(loadComputeProgram(device, shaderProgram, "test-pointer-init", "computeMain", slangReflection));

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
    slang::ProgramLayout* slangReflection = nullptr;
    REQUIRE_CALL(loadComputeProgram(device, shaderProgram, "test-pointer-copy", "computeMain", slangReflection));

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

GPU_TEST_CASE("heap-create", CUDA)
{
    HeapDesc desc;
    desc.label = "Test Graphics Heap";
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));
}

GPU_TEST_CASE("heap-allocate", CUDA)
{
    HeapDesc desc;
    desc.label = "Test Graphics Heap";
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    HeapAllocDesc allocDesc;
    allocDesc.size = 1024 * 1024; // 1 MB
    allocDesc.alignment = 128;

    HeapAlloc allocation;
    REQUIRE_CALL(heap->allocate(allocDesc, &allocation));
    CHECK_EQ(allocation.size, allocDesc.size);

    IHeap::Report report = heap->report();
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

GPU_TEST_CASE("heap-submit", CUDA)
{
    HeapDesc desc;
    desc.label = "Test Graphics Heap";
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));

    HeapAllocDesc allocDesc;
    allocDesc.size = 1024 * 1024; // 1 MB
    allocDesc.alignment = 128;    // 256 KB

    HeapAlloc allocation;
    REQUIRE_CALL(heap->allocate(allocDesc, &allocation));
    CHECK_EQ(allocation.size, allocDesc.size);

    IHeap::Report report = heap->report();
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

GPU_TEST_CASE("heap-pointer-stress-test", CUDA)
{
    ComputePipelineDesc pipelineDesc = {};

    ComPtr<IShaderProgram> initPtrShaderProgram;
    slang::ProgramLayout* initPtrSlangReflection = nullptr;
    REQUIRE_CALL(
        loadComputeProgram(device, initPtrShaderProgram, "test-pointer-init", "computeMain", initPtrSlangReflection)
    );

    pipelineDesc.program = initPtrShaderProgram.get();
    ComPtr<IComputePipeline> initPtrpipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, initPtrpipeline.writeRef()));

    ComPtr<IShaderProgram> copyPtrShaderProgram;
    slang::ProgramLayout* copyPtrSlangReflection = nullptr;
    REQUIRE_CALL(
        loadComputeProgram(device, copyPtrShaderProgram, "test-pointer-copy", "computeMain", copyPtrSlangReflection)
    );
    pipelineDesc.program = copyPtrShaderProgram.get();
    ComPtr<IComputePipeline> copyPtrpipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, copyPtrpipeline.writeRef()));


    HeapDesc desc;
    desc.label = "Test Graphics Heap";
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
        }
    }
}

GPU_TEST_CASE("heap-reports", ALL)
{
    HeapReports reports;
    REQUIRE_CALL(device->reportHeaps(&reports));

    auto deviceType = device->getDeviceType();

    if (deviceType == DeviceType::CUDA)
    {
        // CUDA should report 2 heaps (device and host memory)
        CHECK(reports.heapCount == 2);
        CHECK(reports.heaps != nullptr);

        if (reports.heapCount >= 2)
        {
            // Check that heap names are set
            CHECK(reports.heaps[0].name != nullptr);
            CHECK(reports.heaps[1].name != nullptr);
        }
    }
    else
    {
        // Other devices should report no heaps (default implementation)
        CHECK(reports.heapCount == 0);
        CHECK(reports.heaps == nullptr);
    }
}
