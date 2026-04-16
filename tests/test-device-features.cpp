// test-device-features.cpp
// Device feature tests (e.g. skip pattern when a feature is not available).

#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

// Test that demonstrates skipping when AtomicBfloat16 is not available.
// This pattern should be used by tests that require bfloat16 atomic operations.
GPU_TEST_CASE("atomic-bfloat16", CUDA)
{
    REQUIRE(device);

    if (!device->hasFeature(Feature::AtomicBfloat16))
    {
        SKIP("AtomicBfloat16 not supported (requires SM 9.0/Hopper or newer)");
    }

    // If we reach here, the device supports AtomicBfloat16
    // Double-check that SM 9.0 capability is also present
    CHECK(device->hasCapability(Capability::_cuda_sm_9_0));
    MESSAGE("Running test on device with AtomicBfloat16 support (SM 9.0+)");
    CHECK(true);
}
