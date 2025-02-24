#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("fence-default-value", D3D12 | Vulkan | Metal | WGPU | CPU)
{
    FenceDesc fenceDesc = {};
    ComPtr<IFence> fence;
    REQUIRE_CALL(device->createFence(fenceDesc, fence.writeRef()));
    uint64_t value;
    REQUIRE_CALL(fence->getCurrentValue(&value));
    CHECK(value == 0);
}

GPU_TEST_CASE("fence-initial-value", D3D12 | Vulkan | Metal | WGPU | CPU)
{
    FenceDesc fenceDesc = {};
    fenceDesc.initialValue = 10;
    ComPtr<IFence> fence;
    REQUIRE_CALL(device->createFence(fenceDesc, fence.writeRef()));
    uint64_t value;
    REQUIRE_CALL(fence->getCurrentValue(&value));
    CHECK(value == 10);
}

GPU_TEST_CASE("fence-set-value", D3D12 | Vulkan | Metal | WGPU | CPU)
{
    FenceDesc fenceDesc = {};
    ComPtr<IFence> fence;
    REQUIRE_CALL(device->createFence(fenceDesc, fence.writeRef()));
    REQUIRE_CALL(fence->setCurrentValue(20));
    uint64_t value;
    REQUIRE_CALL(fence->getCurrentValue(&value));
    CHECK(value == 20);
}

GPU_TEST_CASE("fence-wait-without-timeout", D3D12 | Vulkan | Metal | WGPU | CPU)
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

GPU_TEST_CASE("fence-wait-with-timeout", D3D12 | Vulkan | Metal | WGPU | CPU)
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
