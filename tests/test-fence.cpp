#include "testing.h"

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
