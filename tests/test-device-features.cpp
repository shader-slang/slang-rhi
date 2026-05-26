// test-device-features.cpp

#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("device-wave-size-limits", ALL)
{
    REQUIRE(device);

    const DeviceLimits& limits = device->getInfo().limits;

    if (limits.minWaveSize > 0 || limits.maxWaveSize > 0)
    {
        CHECK(limits.minWaveSize > 0);
        CHECK(limits.maxWaveSize >= limits.minWaveSize);
    }

    if (device->hasFeature(Feature::WaveOps))
    {
        CHECK(limits.minWaveSize > 0);
        CHECK(limits.maxWaveSize >= limits.minWaveSize);
    }
}

GPU_TEST_CASE("cuda-device-features", CUDA)
{
    REQUIRE(device);

    CHECK(device->hasFeature(Feature::Pointer));

    const bool has_sm2_0 = device->hasCapability(Capability::_cuda_sm_2_0);
    const bool has_sm3_0 = device->hasCapability(Capability::_cuda_sm_3_0);
    const bool has_sm6_0 = device->hasCapability(Capability::_cuda_sm_6_0);
    const bool has_sm7_0 = device->hasCapability(Capability::_cuda_sm_7_0);
    const bool has_sm8_0 = device->hasCapability(Capability::_cuda_sm_8_0);
    const bool has_sm9_0 = device->hasCapability(Capability::_cuda_sm_9_0);

    // Compute capability 2.0 features
    CHECK(device->hasFeature(Feature::Double) == has_sm2_0);
    CHECK(device->hasFeature(Feature::Int16) == has_sm2_0);
    CHECK(device->hasFeature(Feature::Int64) == has_sm2_0);
    CHECK(device->hasFeature(Feature::AtomicFloat) == has_sm2_0);
    CHECK(device->hasFeature(Feature::AtomicInt64) == has_sm2_0);
    CHECK(device->hasFeature(Feature::RealtimeClock) == has_sm2_0);

    // Compute capability 3.0 features
    CHECK(device->hasFeature(Feature::WaveOps) == has_sm3_0);

    // Compute capability 6.0 features
    CHECK(device->hasFeature(Feature::Half) == has_sm6_0);

    // Compute capability 7.0 features
    CHECK(device->hasFeature(Feature::AtomicHalf) == has_sm7_0);

    // Compute capability 8.0 features
    CHECK(device->hasFeature(Feature::Bfloat16) == has_sm8_0);

    // Compute capability 9.0 features
    CHECK(device->hasFeature(Feature::AtomicBfloat16) == has_sm9_0);
}
