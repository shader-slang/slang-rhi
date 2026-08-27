// Regression test for https://github.com/shader-slang/slang-rhi/issues/844
//
// ConstantBufferPool::reset() used to rewind its cursors only, never releasing the pages it had
// allocated. Because retired command buffers are cached for the queue's lifetime, every cached
// command buffer kept its (4 MiB on Vulkan) constant-buffer pages alive indefinitely, producing
// unbounded, multi-GiB retention. reset() runs only after the command buffer's submission fence
// has signaled, so releasing the pages is safe; this test verifies it now does so.
//
// The test drives the Vulkan ConstantBufferPool directly (it is a white-box unit test): it
// allocates a page, asserts the page exists (a positive control that keeps the test from passing
// vacuously), then resets and asserts the page was released. It is Vulkan-only because it reaches
// into the concrete backend pool; the same reset() change is applied to D3D12, which shares the
// caching lifecycle but cannot run here (Windows-only).

#include "testing.h"

#if SLANG_RHI_ENABLE_VULKAN

#include "../src/vulkan/vk-device.h"
#include "../src/vulkan/vk-buffer.h"
#include "../src/vulkan/vk-constant-buffer-pool.h"
#include "debug-layer/debug-device.h"

using namespace rhi;
using namespace rhi::testing;

// getDevice() returns a DebugDevice wrapper when the debug layer is active; unwrap it to reach the
// concrete Vulkan DeviceImpl that the pool allocates its pages from.
static vk::DeviceImpl* getVulkanDevice(IDevice* device)
{
    if (auto debugDevice = dynamic_cast<debug::DebugDevice*>(device))
        return checked_cast<vk::DeviceImpl*>(debugDevice->baseObject.get());
    return checked_cast<vk::DeviceImpl*>(device);
}

GPU_TEST_CASE("constant-buffer-pool-reset-frees-pages", Vulkan)
{
    vk::DeviceImpl* deviceImpl = getVulkanDevice(device);

    vk::ConstantBufferPool pool;
    pool.init(deviceImpl);
    CHECK(pool.getPageCount() == 0);

    // Allocate enough to force a page to be created. This is the positive control: if the pool
    // never allocated a page, the post-reset assertion below would pass vacuously.
    vk::ConstantBufferPool::Allocation allocation = {};
    REQUIRE_CALL(pool.allocate(1024, allocation));
    CHECK(allocation.buffer != nullptr);
    CHECK(pool.getPageCount() == 1);

    // Before the fix, reset() only rewound cursors and left the page allocated.
    pool.finish();
    pool.reset();
    CHECK(pool.getPageCount() == 0);
}

#endif // SLANG_RHI_ENABLE_VULKAN
