#include "testing.h"
#include <random>
#include "../src/core/span.h"
#include "../src/cuda/cuda-device.h"
#include "../src/cuda/cuda-helper-functions.h"
#include "../src/cuda/cuda-api.h"
#include "../src/cuda/cuda-fence.h"
#include "debug-layer/debug-device.h"
#include "debug-layer/debug-fence.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("fence-default-value", ALL & ~D3D11)
{
    FenceDesc fenceDesc = {};
    ComPtr<IFence> fence;
    REQUIRE_CALL(device->createFence(fenceDesc, fence.writeRef()));
    uint64_t value;
    REQUIRE_CALL(fence->getCurrentValue(&value));
    CHECK(value == 0);
}

GPU_TEST_CASE("fence-initial-value", ALL & ~D3D11)
{
    FenceDesc fenceDesc = {};
    fenceDesc.initialValue = 10;
    ComPtr<IFence> fence;
    REQUIRE_CALL(device->createFence(fenceDesc, fence.writeRef()));
    uint64_t value;
    REQUIRE_CALL(fence->getCurrentValue(&value));
    CHECK(value == 10);
}

GPU_TEST_CASE("fence-set-value", ALL & ~D3D11)
{
    FenceDesc fenceDesc = {};
    ComPtr<IFence> fence;
    REQUIRE_CALL(device->createFence(fenceDesc, fence.writeRef()));
    REQUIRE_CALL(fence->setCurrentValue(20));
    uint64_t value;
    REQUIRE_CALL(fence->getCurrentValue(&value));
    CHECK(value == 20);
}

GPU_TEST_CASE("fence-wait-without-timeout", ALL & ~D3D11)
{
    FenceDesc fenceDesc = {};
    ComPtr<IFence> fence1;
    ComPtr<IFence> fence2;
    REQUIRE_CALL(device->createFence(fenceDesc, fence1.writeRef()));
    REQUIRE_CALL(device->createFence(fenceDesc, fence2.writeRef()));

    // Wait for single signaled fence
    {
        IFence* fences[] = {fence1.get()};
        uint64_t values[] = {0};
        CHECK(device->waitForFences(1, fences, values, false, 0) == SLANG_OK);
        CHECK(device->waitForFences(1, fences, values, true, 0) == SLANG_OK);
    }

    // Wait for single unsignaled fence
    {
        IFence* fences[] = {fence1.get()};
        uint64_t values[] = {1};
        CHECK(device->waitForFences(1, fences, values, false, 0) == SLANG_E_TIME_OUT);
        CHECK(device->waitForFences(1, fences, values, true, 0) == SLANG_E_TIME_OUT);
    }

    // Wait for two signaled fences
    {
        IFence* fences[] = {fence1.get(), fence2.get()};
        uint64_t values[] = {0, 0};
        CHECK(device->waitForFences(2, fences, values, false, 0) == SLANG_OK);
        CHECK(device->waitForFences(2, fences, values, true, 0) == SLANG_OK);
    }

    // Wait for two unsignaled fences
    {
        IFence* fences[] = {fence1.get(), fence2.get()};
        uint64_t values[] = {1, 1};
        CHECK(device->waitForFences(2, fences, values, false, 0) == SLANG_E_TIME_OUT);
        CHECK(device->waitForFences(2, fences, values, true, 0) == SLANG_E_TIME_OUT);
    }

    // Wait for one signaled and one unsigned fences
    {
        IFence* fences[] = {fence1.get(), fence2.get()};
        uint64_t values[] = {0, 1};
        CHECK(device->waitForFences(2, fences, values, false, 0) == SLANG_OK);
        CHECK(device->waitForFences(2, fences, values, true, 0) == SLANG_E_TIME_OUT);
    }
}

GPU_TEST_CASE("fence-wait-with-timeout", ALL & ~D3D11)
{
    FenceDesc fenceDesc = {};
    ComPtr<IFence> fence1;
    ComPtr<IFence> fence2;
    REQUIRE_CALL(device->createFence(fenceDesc, fence1.writeRef()));
    REQUIRE_CALL(device->createFence(fenceDesc, fence2.writeRef()));

    // Wait for single signaled fence
    {
        IFence* fences[] = {fence1.get()};
        uint64_t values[] = {0};
        CHECK(device->waitForFences(1, fences, values, false, 1000) == SLANG_OK);
        CHECK(device->waitForFences(1, fences, values, true, 1000) == SLANG_OK);
    }

    // Wait for single unsignaled fence
    {
        IFence* fences[] = {fence1.get()};
        uint64_t values[] = {1};
        CHECK(device->waitForFences(1, fences, values, false, 1000) == SLANG_E_TIME_OUT);
        CHECK(device->waitForFences(1, fences, values, true, 1000) == SLANG_E_TIME_OUT);
    }

    // Wait for two signaled fences
    {
        IFence* fences[] = {fence1.get(), fence2.get()};
        uint64_t values[] = {0, 0};
        CHECK(device->waitForFences(2, fences, values, false, 1000) == SLANG_OK);
        CHECK(device->waitForFences(2, fences, values, true, 1000) == SLANG_OK);
    }

    // Wait for two unsignaled fences
    {
        IFence* fences[] = {fence1.get(), fence2.get()};
        uint64_t values[] = {1, 1};
        CHECK(device->waitForFences(2, fences, values, false, 1000) == SLANG_E_TIME_OUT);
        CHECK(device->waitForFences(2, fences, values, true, 1000) == SLANG_E_TIME_OUT);
    }

    // Wait for one signaled and one unsigned fences
    {
        IFence* fences[] = {fence1.get(), fence2.get()};
        uint64_t values[] = {0, 1};
        CHECK(device->waitForFences(2, fences, values, false, 1000) == SLANG_OK);
        CHECK(device->waitForFences(2, fences, values, true, 1000) == SLANG_E_TIME_OUT);
    }
}

GPU_TEST_CASE("fence-queue-signal", ALL & ~D3D11)
{
    FenceDesc fenceDesc = {};
    ComPtr<IFence> fence1;
    ComPtr<IFence> fence2;
    REQUIRE_CALL(device->createFence(fenceDesc, fence1.writeRef()));
    REQUIRE_CALL(device->createFence(fenceDesc, fence2.writeRef()));

    IFence* signalFences[] = {fence1, fence2};
    uint64_t signalFenceValues[] = {10, 20};

    SubmitDesc submitDesc = {};
    submitDesc.signalFenceCount = 2;
    submitDesc.signalFences = signalFences;
    submitDesc.signalFenceValues = signalFenceValues;
    REQUIRE_CALL(device->getQueue(QueueType::Graphics)->submit(submitDesc));

    REQUIRE_CALL(device->waitForFences(2, signalFences, signalFenceValues, true, kTimeoutInfinite));

    uint64_t fence1Value, fence2Value;
    REQUIRE_CALL(fence1->getCurrentValue(&fence1Value));
    REQUIRE_CALL(fence2->getCurrentValue(&fence2Value));
    CHECK(fence1Value == 10);
    CHECK(fence2Value == 20);
}

GPU_TEST_CASE("fence-queue-wait", ALL & ~D3D11)
{
    FenceDesc fenceDesc = {};
    ComPtr<IFence> fence1;
    ComPtr<IFence> fence2;
    REQUIRE_CALL(device->createFence(fenceDesc, fence1.writeRef()));
    REQUIRE_CALL(device->createFence(fenceDesc, fence2.writeRef()));

    fence1->setCurrentValue(10);
    fence2->setCurrentValue(20);

    IFence* waitFences[] = {fence1, fence2};
    uint64_t waitFenceValues[] = {10, 20};

    SubmitDesc submitDesc = {};
    submitDesc.waitFenceCount = 2;
    submitDesc.waitFences = waitFences;
    submitDesc.waitFenceValues = waitFenceValues;
    REQUIRE_CALL(device->getQueue(QueueType::Graphics)->submit(submitDesc));
    REQUIRE_CALL(device->getQueue(QueueType::Graphics)->waitOnHost());
}

GPU_TEST_CASE("fence-copy-racecondition", D3D12 | Vulkan | CUDA)
{
    FenceDesc fenceDesc = {};
    ComPtr<IFence> fence;
    REQUIRE_CALL(device->createFence(fenceDesc, fence.writeRef()));

    // Make an epic buffer and populate the last 128 bytes with random data.
    std::vector<uint8_t> data;
    std::mt19937 rng(124112);
    std::uniform_int_distribution<int> dist(0, 255);
    data.resize(128 * 1024 * 1024);
    size_t offset = data.size() - 128;
    for (size_t i = 0; i < 128; i++)
        data[offset + i] = (uint8_t)dist(rng);

    // Setup buffer descriptor
    BufferDesc bufferDesc = {};
    bufferDesc.size = data.size();
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(uint32_t);

    // Create source buffer
    ComPtr<IBuffer> src;
    bufferDesc.usage = BufferUsage::CopySource;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)data.data(), src.writeRef()));

    // Create empty dest buffer
    ComPtr<IBuffer> dst;
    bufferDesc.usage = BufferUsage::CopyDestination;
    bufferDesc.memoryType = MemoryType::ReadBack;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, dst.writeRef()));

    // Map the buffer up front so we have host side access to the shared memory.
    void* mappedData = nullptr;
    REQUIRE_CALL(device->mapBuffer(dst, CpuAccessMode::Read, &mappedData));
    uint8_t* newData = (uint8_t*)mappedData;

    // Wait until all initialization is done
    REQUIRE_CALL(device->getQueue(QueueType::Graphics)->waitOnHost());

    // Use command encoder to copy the whole massive buffer from src to dst
    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();
    commandEncoder->copyBuffer(dst, 0, src, 0, bufferDesc.size);
    auto cb = commandEncoder->finish();

    // Setup submit to run the command buffer and signal the fence
    SubmitDesc submitDesc = {};
    IFence* signalFences[] = {fence};
    uint64_t signalFenceValues[] = {10};
    submitDesc.signalFenceCount = 1;
    submitDesc.signalFences = signalFences;
    submitDesc.signalFenceValues = signalFenceValues;
    ICommandBuffer* commandBuffers[] = {cb};
    submitDesc.commandBufferCount = 1;
    submitDesc.commandBuffers = commandBuffers;
    queue->submit(submitDesc);

    // Check the final few bytes haven't copied yet
    CHECK(memcmp(data.data() + offset, newData + offset, 128) != 0);

    // Wait for the whole device to finish doing its thing
    REQUIRE_CALL(device->waitForFences(1, signalFences, signalFenceValues, true, 1 * 1000 * 1000 * 1000));

    // Check the final byte have now copied
    CHECK(memcmp(data.data() + offset, newData + offset, 128) == 0);
}

// Helpers for getting the wrapped CUDA implementation, handling debug
// devices.
static rhi::cuda::DeviceImpl* getCUDADevice(IDevice* device)
{
    if (auto debugDevice = dynamic_cast<debug::DebugDevice*>(device))
        return (rhi::cuda::DeviceImpl*)debugDevice->baseObject.get();
    else
        return (rhi::cuda::DeviceImpl*)device;
}
static rhi::cuda::FenceImpl* getCUDAFence(IFence* fence)
{
    if (auto debugFence = dynamic_cast<debug::DebugFence*>(fence))
        return (rhi::cuda::FenceImpl*)debugFence->baseObject.get();
    else
        return (rhi::cuda::FenceImpl*)fence;
}

// Test the emulated CUDA fence for host->stream syncing
GPU_TEST_CASE("fence-cuda-hoststream", CUDA)
{
    auto deviceImpl = getCUDADevice(device);

    SLANG_CUDA_CTX_SCOPE(deviceImpl);

    CUstream stream;
    cuStreamCreate(&stream, CU_STREAM_DEFAULT);

    FenceDesc fenceDesc = {};
    ComPtr<IFence> fence;
    REQUIRE_CALL(device->createFence(fenceDesc, fence.writeRef()));

    auto fenceImpl = getCUDAFence(fence.get());

    // Make the stream wait for the fence to signal 10
    fenceImpl->waitOnStream(10, stream);

    // Manually create a cuda event we can poll to see where the stream is
    CUevent event;
    cuEventCreate(&event, CU_EVENT_DEFAULT);
    cuEventRecord(event, stream);

    // Wait for up to 1s, polling the event
    for (int i = 0; i < 1000; i++)
    {
        if (cuEventQuery(event) == CUDA_SUCCESS)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Check the event is still pending
    CHECK(cuEventQuery(event) == CUDA_ERROR_NOT_READY);

    // Now signal the fence from the host
    REQUIRE_CALL(fence->setCurrentValue(10));

    // Wait for up to 1s, polling the event
    for (int i = 0; i < 1000; i++)
    {
        if (cuEventQuery(event) == CUDA_SUCCESS)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Check the event passed
    CHECK(cuEventQuery(event) == CUDA_SUCCESS);
}
