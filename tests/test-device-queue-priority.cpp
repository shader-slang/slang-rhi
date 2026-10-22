#include "testing.h"

#if SLANG_RHI_ENABLE_D3D12
#include <d3d12.h>
#endif
#if SLANG_RHI_ENABLE_CUDA
#include "cuda/cuda-device.h"
#include "cuda/cuda-utils.h"
#endif

using namespace rhi;
using namespace rhi::testing;

TEST_CASE("device-queue-priority-invalid")
{
    DeviceDesc desc = {};
    desc.queuePriority = QueuePriority(-1);
    ComPtr<IDevice> device;
    CHECK(getRHI()->createDevice(desc, device.writeRef()) == SLANG_E_INVALID_ARG);
    CHECK_FALSE(device);
}

TEST_CASE("device-queue-priority-unsupported")
{
    // These requests have no backend mapping and must fail even without a native device.
    for (DeviceType type :
         {DeviceType::CPU, DeviceType::D3D11, DeviceType::Metal, DeviceType::WGPU, DeviceType::D3D12, DeviceType::CUDA})
    {
        CAPTURE(type);
        DeviceDesc desc = {};
        desc.deviceType = type;
        desc.queuePriority = type == DeviceType::CUDA ? QueuePriority::Realtime : QueuePriority::Low;
        ComPtr<IDevice> device;
        CHECK(getRHI()->createDevice(desc, device.writeRef()) == SLANG_E_NOT_AVAILABLE);
        CHECK_FALSE(device);
    }
}

static void checkPriorityQueue(IDevice* device)
{
    auto queue = device->getQueue(QueueType::Graphics);
    REQUIRE(queue);
    // Exercise actual work and readback, including CUDA heaps on an owned stream.
    BufferDesc bufferDesc = {};
    bufferDesc.size = 16;
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::CopySource | BufferUsage::CopyDestination | BufferUsage::UnorderedAccess;
    bufferDesc.defaultState = ResourceState::CopyDestination;
    auto buffer = device->createBuffer(bufferDesc);
    REQUIRE(buffer);

    ComPtr<IQueryPool> pool;
    if (device->hasFeature(Feature::TimestampQuery))
    {
        QueryPoolDesc desc = {};
        desc.count = 2;
        REQUIRE_CALL(device->createQueryPool(desc, pool.writeRef()));
    }
    auto encoder = queue->createCommandEncoder();
    if (pool)
        encoder->writeTimestamp(pool, 0);
    encoder->clearBuffer(buffer, 0, bufferDesc.size);
    if (pool)
        encoder->writeTimestamp(pool, 1);
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    compareComputeResult(device, buffer, makeArray<uint32_t>(0, 0, 0, 0));
    if (pool)
    {
        uint64_t values[2] = {};
        REQUIRE_CALL(pool->getResult(0, 2, values));
        double seconds = -1.0;
        REQUIRE_CALL(device->getInfo().timestampInfo.getElapsedSeconds(values[0], values[1], &seconds));
        CHECK(seconds >= 0.0);
    }
}

GPU_TEST_CASE("device-queue-priority-normal", ALL)
{
    DeviceExtraOptions options;
    options.queuePriority = QueuePriority::Normal;
    auto priorityDevice = createTestingDevice(ctx, ctx->deviceType, false, &options);
    REQUIRE(priorityDevice);
    checkPriorityQueue(priorityDevice);
#if SLANG_RHI_ENABLE_D3D12
    if (ctx->deviceType == DeviceType::D3D12)
    {
        NativeHandle handle;
        REQUIRE_CALL(priorityDevice->getQueue(QueueType::Graphics)->getNativeHandle(&handle));
        CHECK(
            reinterpret_cast<ID3D12CommandQueue*>(handle.value)->GetDesc().Priority ==
            D3D12_COMMAND_QUEUE_PRIORITY_NORMAL
        );
    }
#endif
#if SLANG_RHI_ENABLE_CUDA
    if (ctx->deviceType == DeviceType::CUDA)
    {
        auto impl = static_cast<rhi::cuda::DeviceImpl*>(getUnderlyingDevice(priorityDevice));
        SLANG_CUDA_CTX_SCOPE(impl);
        NativeHandle handle;
        REQUIRE_CALL(priorityDevice->getQueue(QueueType::Graphics)->getNativeHandle(&handle));
        REQUIRE(handle.value != 0);
        int priority = 123;
        REQUIRE(cuStreamGetPriority(reinterpret_cast<CUstream>(handle.value), &priority) == CUDA_SUCCESS);
        CHECK(priority == 0);
    }
#endif
}

#if SLANG_RHI_ENABLE_CUDA
GPU_TEST_CASE("device-queue-priority-cuda-range", CUDA)
{
    auto impl = static_cast<rhi::cuda::DeviceImpl*>(getUnderlyingDevice(device));
    SLANG_CUDA_CTX_SCOPE(impl);
    int least = 0;
    int greatest = 0;
    REQUIRE(cuCtxGetStreamPriorityRange(&least, &greatest) == CUDA_SUCCESS);
    if (least == greatest)
        SKIP("Distinct CUDA stream priorities are unavailable");

    for (QueuePriority priority : {QueuePriority::Low, QueuePriority::High})
    {
        DeviceExtraOptions options;
        options.queuePriority = priority;
        auto priorityDevice = createTestingDevice(ctx, ctx->deviceType, false, &options);
        REQUIRE(priorityDevice);
        auto priorityImpl = static_cast<rhi::cuda::DeviceImpl*>(getUnderlyingDevice(priorityDevice));
        rhi::cuda::ContextScope priorityScope(priorityImpl);
        NativeHandle handle;
        REQUIRE_CALL(priorityDevice->getQueue(QueueType::Graphics)->getNativeHandle(&handle));
        REQUIRE(handle.value != 0);
        int actual = 123;
        REQUIRE(cuStreamGetPriority(reinterpret_cast<CUstream>(handle.value), &actual) == CUDA_SUCCESS);
        CHECK(actual == (priority == QueuePriority::Low ? least : greatest));
        checkPriorityQueue(priorityDevice);
    }
}
#endif

GPU_TEST_CASE("device-queue-priority-imported-vulkan", Vulkan)
{
    DeviceDesc desc = {};
    desc.deviceType = DeviceType::Vulkan;
    REQUIRE_CALL(device->getNativeDeviceHandles(&desc.existingDeviceHandles));
    desc.queuePriority = QueuePriority::Normal;
    ComPtr<IDevice> imported;
    CHECK(getRHI()->createDevice(desc, imported.writeRef()) == SLANG_E_NOT_AVAILABLE);
    CHECK_FALSE(imported);
}

static void checkNativePriorityRequest(IDevice* device, QueuePriority priority)
{
    struct Callback : IDebugCallback
    {
        std::string errors;
        void SLANG_MCALL handleMessage(DebugMessageType type, DebugMessageSource, const char* message) override
        {
            if (type == DebugMessageType::Error)
                errors += message;
        }
    } callback;
    DeviceDesc desc = {};
    desc.deviceType = device->getDeviceType();
    desc.queuePriority = priority;
    desc.debugCallback = &callback;
    desc.slang.slangGlobalSession = device->getSlangSession()->getGlobalSession();
    REQUIRE_CALL(device->getNativeDeviceHandles(&desc.existingDeviceHandles));
    // Reuse the Vulkan physical device/instance, but create a new logical device and queue.
    if (desc.deviceType == DeviceType::Vulkan)
        desc.existingDeviceHandles.handles[2] = {};
    ComPtr<IDevice> priorityDevice;
    Result result = getRHI()->createDevice(desc, priorityDevice.writeRef());
    if (SLANG_FAILED(result))
    {
        CHECK_FALSE(priorityDevice);
        INFO(callback.errors);
        SKIP("Native queue priority unavailable or denied by the driver");
    }
    REQUIRE(priorityDevice);
#if SLANG_RHI_ENABLE_D3D12
    if (desc.deviceType == DeviceType::D3D12)
    {
        NativeHandle handle;
        REQUIRE_CALL(priorityDevice->getQueue(QueueType::Graphics)->getNativeHandle(&handle));
        CHECK(
            reinterpret_cast<ID3D12CommandQueue*>(handle.value)->GetDesc().Priority ==
            (priority == QueuePriority::High ? D3D12_COMMAND_QUEUE_PRIORITY_HIGH
                                             : D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME)
        );
    }
#endif
    checkPriorityQueue(priorityDevice);
}

GPU_TEST_CASE("device-queue-priority-high", D3D12 | Vulkan)
{
    checkNativePriorityRequest(device, QueuePriority::High);
}

GPU_TEST_CASE("device-queue-priority-realtime", D3D12 | Vulkan)
{
    checkNativePriorityRequest(device, QueuePriority::Realtime);
}

GPU_TEST_CASE("device-queue-priority-low", Vulkan)
{
    checkNativePriorityRequest(device, QueuePriority::Low);
}
