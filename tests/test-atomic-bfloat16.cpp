// test-atomic-bfloat16.cpp
// Tests for AtomicBfloat16 feature detection on CUDA devices.
// This feature requires SM 9.0 (Hopper) or higher.

#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

// Test that AtomicBfloat16 feature is reported correctly based on CUDA SM version.
// On SM 9.0+ (Hopper or newer), the feature should be available.
// On older architectures, the feature should not be available and tests requiring it should skip.
GPU_TEST_CASE("atomic-bfloat16-feature-detection", CUDA)
{
    REQUIRE(device);

    // Query device info for logging
    const DeviceInfo& info = device->getInfo();

    // Check both the feature and the underlying capability
    bool hasAtomicBfloat16 = device->hasFeature(Feature::AtomicBfloat16);
    bool hasSM90 = device->hasCapability(Capability::_cuda_sm_9_0);

    // Log the device info for debugging
    MESSAGE("CUDA Device: " << info.adapterName);
    MESSAGE("SM 9.0 capability: " << (hasSM90 ? "yes" : "no"));
    MESSAGE("AtomicBfloat16 feature: " << (hasAtomicBfloat16 ? "yes" : "no"));

    // CRITICAL CHECK: AtomicBfloat16 feature should only be reported when SM 9.0+ is available
    // If AtomicBfloat16 is true, SM 9.0 must also be true
    if (hasAtomicBfloat16)
    {
        CHECK_MESSAGE(hasSM90, "AtomicBfloat16 feature requires SM 9.0 capability");
        MESSAGE("AtomicBfloat16 is correctly reported as supported on SM 9.0+ device.");
    }

    // If SM 9.0 is available, AtomicBfloat16 should also be available
    // (our implementation adds both together)
    if (hasSM90)
    {
        CHECK_MESSAGE(hasAtomicBfloat16, "SM 9.0+ device should report AtomicBfloat16 feature");
    }

    // If neither is available, that's also valid - just means older hardware
    if (!hasAtomicBfloat16 && !hasSM90)
    {
        MESSAGE("Device does not support SM 9.0. AtomicBfloat16 tests will be skipped.");
    }

    // Test always passes - this is a feature detection/verification test
    CHECK(true);
}

// Test that demonstrates skipping when AtomicBfloat16 is not available.
// This pattern should be used by tests that require bfloat16 atomic operations.
GPU_TEST_CASE("atomic-bfloat16-skip-pattern", CUDA)
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
