#include "testing.h"

#if SLANG_RHI_ENABLE_CUDA

#include "core/deferred.h"
#include "cuda/cuda-device.h"
#include "cuda/cuda-heap.h"
#include "cuda/cuda-utils.h"

#include <array>
#include <limits>

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("cuda-dedicated-heap-boundaries", CUDA)
{
    HeapDesc desc;
    desc.caching.enabled = false;
    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));
    constexpr Size pageSize = cuda::HeapImpl::kMaxPooledAllocationSize;
    for (Size size : {pageSize - 128, pageSize, pageSize + 1})
    {
        HeapAllocDesc allocDesc;
        allocDesc.size = size;
        HeapAlloc allocation;
        REQUIRE_CALL(heap->allocate(allocDesc, &allocation));
        Size alignedSize = (size + 127) & ~Size(127);
        CHECK_EQ(allocation.size, alignedSize);
        CHECK_EQ(allocation.address % 128, 0);
        CHECK_EQ(allocation.pageId == nullptr, size > pageSize);
        auto report = heap->report();
        CHECK_EQ(report.totalAllocated, alignedSize);
        CHECK_EQ(report.totalMemUsage, size > pageSize ? alignedSize : pageSize);
        CHECK_EQ(report.numAllocations, 1);
        REQUIRE_CALL(heap->free(allocation));
        REQUIRE_CALL(heap->removeEmptyPages());
        CHECK_EQ(heap->report().totalMemUsage, 0);
    }

    // Small and dedicated allocations coexist, but only the small block leaves a
    // reusable backing page after free. Dedicated allocations never enter a cache.
    HeapAllocDesc allocDesc;
    allocDesc.size = 128;
    HeapAlloc small, large;
    REQUIRE_CALL(heap->allocate(allocDesc, &small));
    allocDesc.size = pageSize + 128;
    REQUIRE_CALL(heap->allocate(allocDesc, &large));
    CHECK_EQ(heap->report().numAllocations, 2);
    CHECK_EQ(heap->report().totalMemUsage, pageSize + large.size);
    REQUIRE_CALL(heap->free(large));
    CHECK_EQ(heap->report().totalMemUsage, pageSize);
    REQUIRE_CALL(heap->free(small));
    REQUIRE_CALL(heap->removeEmptyPages());

    // Invalid sizes must fail before CUDA is called, without publishing ownership
    // or allowing alignment arithmetic to wrap a huge request into a small one.
    allocDesc.size = std::numeric_limits<Size>::max();
    large = small;
    CHECK_EQ(heap->allocate(allocDesc, &large), SLANG_E_INVALID_ARG);
    CHECK(!large);
    CHECK_EQ(heap->report().numAllocations, 0);
}

GPU_TEST_CASE("cuda-dedicated-host-heap", CUDA)
{
    for (MemoryType memoryType : {MemoryType::Upload, MemoryType::ReadBack})
    {
        HeapDesc desc;
        desc.memoryType = memoryType;
        ComPtr<IHeap> heap;
        REQUIRE_CALL(device->createHeap(desc, heap.writeRef()));
        HeapAllocDesc allocDesc;
        allocDesc.size = cuda::HeapImpl::kMaxPooledAllocationSize + 128;
        HeapAlloc allocation;
        REQUIRE_CALL(heap->allocate(allocDesc, &allocation));
        REQUIRE(allocation.pageId == nullptr);
        auto bytes = static_cast<uint8_t*>(allocation.getHostPtr());
        bytes[0] = 17;
        bytes[allocation.size - 1] = 42;
        CHECK_EQ(bytes[0], 17);
        CHECK_EQ(bytes[allocation.size - 1], 42);
        REQUIRE_CALL(heap->free(allocation));
        CHECK_EQ(heap->report().totalMemUsage, 0);
    }
}

GPU_TEST_CASE("cuda-dedicated-allocation-failure", CUDA)
{
    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(HeapDesc{}, heap.writeRef()));
    HeapAllocDesc desc;
    desc.size = cuda::HeapImpl::kMaxPooledAllocationSize + 128;
    HeapAlloc allocation;
    // Inject an allocation failure instead of exhausting the user's GPU. Restore
    // the driver entry point even when a REQUIRE aborts the test's scope.
    auto originalAlloc = cuMemAlloc;
    SLANG_RHI_DEFERRED({ cuMemAlloc = originalAlloc; });
    cuMemAlloc = [](CUdeviceptr*, size_t) -> CUresult
    {
        return CUDA_ERROR_OUT_OF_MEMORY;
    };
    ScopedDisableNativeCallError disableExpectedError;
    CHECK_EQ(heap->allocate(desc, &allocation), SLANG_E_OUT_OF_MEMORY);
    CHECK(!allocation);
    CHECK_EQ(heap->report().numAllocations, 0);
    CHECK_EQ(heap->report().totalMemUsage, 0);
}

GPU_TEST_CASE("cuda-dedicated-heap-deferred-free", CUDA)
{
    auto queue = device->getQueue(QueueType::Graphics);
    REQUIRE_CALL(queue->waitOnHost());
    ComPtr<IHeap> heap;
    REQUIRE_CALL(device->createHeap(HeapDesc{}, heap.writeRef()));
    HeapAllocDesc desc;
    desc.size = cuda::HeapImpl::kMaxPooledAllocationSize + 128;
    HeapAlloc allocation;
    REQUIRE_CALL(heap->allocate(desc, &allocation));

    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-pointer-init", "computeMain", program.writeRef()));
    ComputePipelineDesc pipelineDesc;
    pipelineDesc.program = program;
    auto pipeline = device->createComputePipeline(pipelineDesc);
    REQUIRE(pipeline);
    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    ShaderCursor cursor(pass->bindPipeline(pipeline));
    REQUIRE_CALL(cursor["val"].setData(42u));
    REQUIRE_CALL(cursor["dst"].setData(allocation.getDeviceAddress()));
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(heap->free(allocation));
    // A direct IHeap is not part of the device's automatic heap flush list.
    // Its allocation stays accounted for until explicit completion and flush.
    CHECK_EQ(heap->report().totalAllocated, allocation.size);
    REQUIRE_CALL(queue->waitOnHost());
    REQUIRE_CALL(heap->flush());
    CHECK_EQ(heap->report().totalMemUsage, 0);

    // Destruction also releases dedicated storage without needing an explicit free.
    REQUIRE_CALL(heap->allocate(desc, &allocation));
    heap.setNull();
}

GPU_TEST_CASE("cuda-dedicated-buffer-recorded-lifetime", CUDA)
{
    auto queue = device->getQueue(QueueType::Graphics);
    REQUIRE_CALL(queue->waitOnHost());
    auto cudaDevice = checked_cast<cuda::DeviceImpl*>(getUnderlyingDevice(device));
    auto heap = cudaDevice->m_deviceMemHeap.get();
    size_t originalCount = heap->m_dedicatedAllocations.size();

    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-cuda-dedicated-allocation", "computeMain", program.writeRef()));
    ComputePipelineDesc pipelineDesc;
    pipelineDesc.program = program;
    auto pipeline = device->createComputePipeline(pipelineDesc);
    REQUIRE(pipeline);
    BufferDesc desc;
    desc.size = cuda::HeapImpl::kMaxPooledAllocationSize + 128;
    desc.elementSize = sizeof(uint32_t);
    desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    auto storage = device->createBuffer(desc);
    REQUIRE(storage);
    uint32_t count = uint32_t(desc.size / sizeof(uint32_t));
    desc.size = 4 * sizeof(uint32_t);
    auto result = device->createBuffer(desc);
    REQUIRE(result);
    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    ShaderCursor cursor(pass->bindPipeline(pipeline));
    REQUIRE_CALL(cursor["storage"].setBinding(storage));
    REQUIRE_CALL(cursor["result"].setBinding(result));
    REQUIRE_CALL(cursor["index"].setData(count - 1));
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    auto commands = encoder->finish();

    // Drop shader-object and caller ownership after recording. Waiting BEFORE
    // submission drains deferred deletes, exposing missing command-buffer retention.
    REQUIRE_CALL(cursor["storage"].setBinding(Binding((IBuffer*)nullptr)));
    storage.setNull();
    encoder.setNull();
    REQUIRE_CALL(queue->waitOnHost());
    REQUIRE_EQ(heap->m_dedicatedAllocations.size(), originalCount + 1);
    REQUIRE_CALL(queue->submit(commands));
    commands.setNull();
    REQUIRE_CALL(queue->waitOnHost());
    CHECK_EQ(heap->m_dedicatedAllocations.size(), originalCount);
    compareComputeResult(device, result, std::array<uint32_t, 4>{17, 42, count, 4});
}

GPU_TEST_CASE("cuda-dedicated-buffer-over-4gib", CUDA)
{
    auto cudaDevice = checked_cast<cuda::DeviceImpl*>(getUnderlyingDevice(device));
    cuda::ContextScope context(cudaDevice);
    constexpr Size size = (Size(1) << 32) + 256;
    size_t freeBytes = 0, totalBytes = 0;
    REQUIRE_EQ(cuMemGetInfo(&freeBytes, &totalBytes), CUDA_SUCCESS);
    if (freeBytes < size + cuda::HeapImpl::kMaxPooledAllocationSize * 2)
        SKIP("Requires over 4 GiB of available CUDA device memory");

    BufferDesc desc;
    desc.size = size;
    desc.elementSize = sizeof(uint32_t);
    desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    auto storage = device->createBuffer(desc);
    REQUIRE(storage);
    CHECK_EQ(storage->getDesc().size, size);
    desc.size = 4 * sizeof(uint32_t);
    auto result = device->createBuffer(desc);
    REQUIRE(result);
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-cuda-dedicated-allocation", "computeMain", program.writeRef()));
    ComputePipelineDesc pipelineDesc;
    pipelineDesc.program = program;
    auto pipeline = device->createComputePipeline(pipelineDesc);
    REQUIRE(pipeline);
    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    ShaderCursor cursor(pass->bindPipeline(pipeline));
    REQUIRE_CALL(cursor["storage"].setBinding(storage));
    REQUIRE_CALL(cursor["result"].setBinding(result));
    REQUIRE_CALL(cursor["index"].setData(uint32_t(size / sizeof(uint32_t) - 1)));
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    compareComputeResult(device, result, std::array<uint32_t, 4>{17, 42, uint32_t(size / 4), 4});
    // Sparse readback checks the full 64-bit buffer offset without allocating a
    // matching multi-GiB CPU vector or filling the entire GPU allocation.
    uint32_t marker = 0;
    REQUIRE_CALL(device->readBuffer(storage, size - sizeof(marker), sizeof(marker), &marker));
    CHECK_EQ(marker, 42);
}

#endif
