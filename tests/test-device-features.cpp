// test-device-features.cpp

#include "testing.h"

#if SLANG_RHI_ENABLE_METAL
#include <Foundation/NSProcessInfo.hpp>
#endif

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

    // Slang's optix_coopvec capability implies _cuda_sm_9_0. OptiX can expose
    // cooperative-vector API support on devices below that compute capability,
    // where Slang must continue to use its generic CUDA lowering.
    CHECK(
        device->hasCapability(Capability::optix_coopvec) ==
        (device->hasFeature(Feature::CooperativeVector) && has_sm9_0)
    );
}

#if SLANG_RHI_ENABLE_METAL
GPU_TEST_CASE("metal-device-capabilities", Metal)
{
    REQUIRE(device);

    const auto osVersion = NS::ProcessInfo::processInfo()->operatingSystemVersion();
    const auto macOSMajorVersion = osVersion.majorVersion;

    CHECK(device->hasCapability(Capability::metallib_2_3) == (macOSMajorVersion >= 11));
    CHECK(device->hasCapability(Capability::metallib_2_4) == (macOSMajorVersion >= 12));
    CHECK(device->hasCapability(Capability::metallib_3_0) == (macOSMajorVersion >= 13));
    CHECK(device->hasCapability(Capability::metallib_3_1) == (macOSMajorVersion >= 14));
    CHECK(device->hasCapability(Capability::metallib_3_2) == (macOSMajorVersion >= 15));
    // TODO: Temporarily disabled until Slang's Metal 4.0 output selects the Metal 4.0 downstream language standard.
    // https://github.com/shader-slang/slang/issues/12325
    // CHECK(device->hasCapability(Capability::metallib_4_0) == (macOSMajorVersion >= 26));
}
#endif

// The coop-mat2 subfeature name strings are a cross-repo contract: shader-slang/slang's runtime
// tests gate on these exact `-render-feature` names, so a typo here silently breaks gating (an
// unknown name is a loud failure, a renamed one skips the wrong tests) rather than failing to
// compile. Pin the strings so a future rename is caught here. Device-independent by design.
TEST_CASE("cooperative-matrix-2-subfeature-names")
{
    IRHI* rhi = getRHI();
    CHECK(std::string(rhi->getFeatureName(Feature::CooperativeMatrixReductions)) == "cooperative-matrix-reductions");
    CHECK(std::string(rhi->getFeatureName(Feature::CooperativeMatrixConversions)) == "cooperative-matrix-conversions");
    CHECK(
        std::string(rhi->getFeatureName(Feature::CooperativeMatrixPerElementOperations)) ==
        "cooperative-matrix-per-element-operations"
    );
    CHECK(
        std::string(rhi->getFeatureName(Feature::CooperativeMatrixTensorAddressing)) ==
        "cooperative-matrix-tensor-addressing"
    );
    CHECK(std::string(rhi->getFeatureName(Feature::CooperativeMatrixBlockLoads)) == "cooperative-matrix-block-loads");
}
