#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

namespace {

struct CallbackPayload
{
    uint32_t value;
};

struct CallbackRecord
{
    uint32_t callCount = 0;
    std::array<NativeHandle, 4> handles = {};
    std::array<uint32_t, 4> payloadValues = {};
    std::array<Size, 4> payloadSizes = {};
    std::array<bool, 4> retainedDuringCallback = {};
    uint32_t retainCount = 0;
    uint32_t releaseCount = 0;
};

NativeHandleType getExpectedNativeHandleType(DeviceType deviceType)
{
    switch (deviceType)
    {
    case DeviceType::D3D11:
        return NativeHandleType::D3D11DeviceContext;
    case DeviceType::D3D12:
        return NativeHandleType::D3D12GraphicsCommandList;
    case DeviceType::Vulkan:
        return NativeHandleType::VkCommandBuffer;
    case DeviceType::Metal:
        return NativeHandleType::MTLCommandBuffer;
    case DeviceType::CUDA:
        return NativeHandleType::CUstream;
    case DeviceType::WGPU:
        return NativeHandleType::WGPUCommandEncoder;
    default:
        return NativeHandleType::Undefined;
    }
}

bool releasesCallbackObjectsOnQueueWait(DeviceType deviceType)
{
    switch (deviceType)
    {
    case DeviceType::D3D12:
    case DeviceType::Vulkan:
    case DeviceType::Metal:
    case DeviceType::CUDA:
        return true;
    default:
        return false;
    }
}

void SLANG_MCALL executeCallback(
    const ExecuteCallbackContext* context,
    void* userObject,
    const void* userData,
    Size userDataSize
)
{
    CallbackRecord* record = static_cast<CallbackRecord*>(userObject);
    const CallbackPayload* payload = static_cast<const CallbackPayload*>(userData);
    uint32_t index = record->callCount++;

    if (index < record->handles.size())
    {
        record->handles[index] = context ? context->nativeHandle : NativeHandle{};
        record->payloadValues[index] = payload ? payload->value : 0;
        record->payloadSizes[index] = userDataSize;
        record->retainedDuringCallback[index] = record->retainCount > record->releaseCount;
    }
}

void SLANG_MCALL retainCallbackObject(void* userObject)
{
    CallbackRecord* record = static_cast<CallbackRecord*>(userObject);
    record->retainCount++;
}

void SLANG_MCALL releaseCallbackObject(void* userObject)
{
    CallbackRecord* record = static_cast<CallbackRecord*>(userObject);
    record->releaseCount++;
}

void addExecuteCallback(ICommandEncoder* encoder, CallbackRecord* record, const CallbackPayload& payload)
{
    ExecuteCallbackDesc desc = {};
    desc.callback = executeCallback;
    desc.userObject = record;
    desc.retainUserObject = retainCallbackObject;
    desc.releaseUserObject = releaseCallbackObject;
    desc.userData = &payload;
    desc.userDataSize = sizeof(payload);
    encoder->executeCallback(desc);
}

} // namespace

GPU_TEST_CASE("execute-callback-native-handle-and-user-data", ALL)
{
    auto queue = device->getQueue(QueueType::Graphics);
    REQUIRE(queue);

    auto encoder = queue->createCommandEncoder();
    REQUIRE(encoder);

    CallbackRecord record;
    CallbackPayload firstPayload = {0x12345678u};
    CallbackPayload secondPayload = {0x87654321u};
    addExecuteCallback(encoder, &record, firstPayload);
    addExecuteCallback(encoder, &record, secondPayload);

    firstPayload.value = 0;
    secondPayload.value = 0;

    auto commandBuffer = encoder->finish();
    REQUIRE(commandBuffer);

    REQUIRE_CALL(queue->submit(commandBuffer));
    REQUIRE_CALL(queue->waitOnHost());

    CHECK(record.callCount == 2);
    CHECK(record.payloadValues[0] == 0x12345678u);
    CHECK(record.payloadValues[1] == 0x87654321u);
    CHECK(record.payloadSizes[0] == sizeof(CallbackPayload));
    CHECK(record.payloadSizes[1] == sizeof(CallbackPayload));

    NativeHandleType expectedType = getExpectedNativeHandleType(ctx->deviceType);
    CHECK(record.handles[0].type == expectedType);
    CHECK(record.handles[1].type == expectedType);

    if (ctx->deviceType == DeviceType::CUDA)
    {
        NativeHandle queueHandle = {};
        REQUIRE_CALL(queue->getNativeHandle(&queueHandle));
        CHECK(record.handles[0].value == queueHandle.value);
        CHECK(record.handles[1].value == queueHandle.value);
    }
    else if (ctx->deviceType == DeviceType::D3D12 || ctx->deviceType == DeviceType::Vulkan ||
             ctx->deviceType == DeviceType::Metal)
    {
        NativeHandle commandBufferHandle = {};
        REQUIRE_CALL(commandBuffer->getNativeHandle(&commandBufferHandle));
        CHECK(record.handles[0].value == commandBufferHandle.value);
        CHECK(record.handles[1].value == commandBufferHandle.value);
        CHECK(record.handles[0].value != 0);
        CHECK(record.handles[1].value != 0);
    }
    else if (expectedType == NativeHandleType::Undefined)
    {
        CHECK(record.handles[0].value == 0);
        CHECK(record.handles[1].value == 0);
    }
    else
    {
        CHECK(record.handles[0].value != 0);
        CHECK(record.handles[1].value != 0);
    }

    if (releasesCallbackObjectsOnQueueWait(ctx->deviceType))
    {
        CHECK(record.releaseCount == 2);
    }
    else
    {
        CHECK(record.releaseCount == 0);
        commandBuffer.setNull();
        CHECK(record.releaseCount == 2);
    }
}

GPU_TEST_CASE("execute-callback-object-lifetime", ALL)
{
    auto queue = device->getQueue(QueueType::Graphics);
    REQUIRE(queue);

    auto encoder = queue->createCommandEncoder();
    REQUIRE(encoder);

    CallbackRecord record;
    CallbackPayload payload = {0xf00dcafeu};
    addExecuteCallback(encoder, &record, payload);

    CHECK(record.retainCount == 1);
    CHECK(record.releaseCount == 0);

    auto commandBuffer = encoder->finish();
    REQUIRE(commandBuffer);
    CHECK(record.releaseCount == 0);

    REQUIRE_CALL(queue->submit(commandBuffer));
    REQUIRE_CALL(queue->waitOnHost());

    CHECK(record.callCount == 1);
    CHECK(record.payloadValues[0] == 0xf00dcafeu);
    CHECK(record.payloadSizes[0] == sizeof(CallbackPayload));
    CHECK(record.retainedDuringCallback[0]);
    CHECK(record.retainCount == 1);
    if (releasesCallbackObjectsOnQueueWait(ctx->deviceType))
    {
        CHECK(record.releaseCount == 1);
    }
    else
    {
        CHECK(record.releaseCount == 0);
    }

    commandBuffer.setNull();
    CHECK(record.releaseCount == 1);
}
