// test-device-features.cpp

#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("cuda-device-features", CUDA)
{
    REQUIRE(device);

    const bool has_sm2_0 = device->hasCapability(Capability::_cuda_sm_2_0);
    const bool has_sm3_0 = device->hasCapability(Capability::_cuda_sm_3_0);
    const bool has_sm6_0 = device->hasCapability(Capability::_cuda_sm_6_0);
    const bool has_sm9_0 = device->hasCapability(Capability::_cuda_sm_9_0);

    if (device->hasFeature(Feature::Double))
        CHECK(has_sm2_0);
    if (device->hasFeature(Feature::Int64))
        CHECK(has_sm2_0);
    if (device->hasFeature(Feature::AtomicFloat))
        CHECK(has_sm2_0);
    if (device->hasFeature(Feature::RealtimeClock))
        CHECK(has_sm2_0);
    if (device->hasFeature(Feature::WaveOps))
        CHECK(has_sm3_0);
    if (device->hasFeature(Feature::Half))
        CHECK(has_sm6_0);
    if (device->hasFeature(Feature::Int16))
        CHECK(has_sm6_0);
    if (device->hasFeature(Feature::AtomicHalf))
        CHECK(has_sm6_0);
    if (device->hasFeature(Feature::AtomicInt64))
        CHECK(has_sm6_0);
    if (device->hasFeature(Feature::AtomicBfloat16))
        CHECK(has_sm9_0);

    CHECK(device->hasFeature(Feature::Pointer));
}
