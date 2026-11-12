// test-device-features.cpp

#include "testing.h"

#if SLANG_RHI_ENABLE_VULKAN
#include "vulkan/vk-api.h"
#endif

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

#if SLANG_RHI_ENABLE_VULKAN
GPU_TEST_CASE("vulkan-device-capabilities", Vulkan)
{
    DeviceNativeHandles handles;
    REQUIRE_CALL(device->getNativeDeviceHandles(&handles));

    vk::VulkanModule module;
    REQUIRE_CALL(module.init());
    vk::VulkanApi api;
    REQUIRE_CALL(api.initGlobalProcs(module));
    REQUIRE_CALL(api.initInstanceProcs((VkInstance)handles.handles[0].value));
    VkPhysicalDevice physicalDevice = (VkPhysicalDevice)handles.handles[1].value;
    VkPhysicalDeviceProperties basicProperties = {};
    api.vkGetPhysicalDeviceProperties(physicalDevice, &basicProperties);
    // The backend reports these capabilities only through its Vulkan 1.1+ feature-query path.
    if (basicProperties.apiVersion < VK_API_VERSION_1_1 || !api.vkGetPhysicalDeviceProperties2 ||
        !api.vkGetPhysicalDeviceFeatures2)
    {
        SKIP("Vulkan 1.1 feature and property queries are not available");
    }

    VkPhysicalDeviceSubgroupProperties subgroups = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &subgroups;
    api.vkGetPhysicalDeviceProperties2(physicalDevice, &properties);

    // Compare the public RHI capabilities against the driver's individual operation bits, rather
    // than the approximate WaveOps feature (which can be true on devices with only basic support).
    const std::pair<VkSubgroupFeatureFlagBits, Capability> operations[] = {
        {VK_SUBGROUP_FEATURE_BASIC_BIT, Capability::spvGroupNonUniform},
        {VK_SUBGROUP_FEATURE_VOTE_BIT, Capability::spvGroupNonUniformVote},
        {VK_SUBGROUP_FEATURE_ARITHMETIC_BIT, Capability::spvGroupNonUniformArithmetic},
        {VK_SUBGROUP_FEATURE_BALLOT_BIT, Capability::spvGroupNonUniformBallot},
        {VK_SUBGROUP_FEATURE_SHUFFLE_BIT, Capability::spvGroupNonUniformShuffle},
        {VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT, Capability::spvGroupNonUniformShuffleRelative},
        {VK_SUBGROUP_FEATURE_CLUSTERED_BIT, Capability::spvGroupNonUniformClustered},
        {VK_SUBGROUP_FEATURE_QUAD_BIT, Capability::spvGroupNonUniformQuad},
    };
    for (const auto& [operation, capability] : operations)
    {
        const char* capabilityName = getRHI()->getCapabilityName(capability);
        CAPTURE(capabilityName);
        CHECK(device->hasCapability(capability) == bool(subgroups.supportedOperations & operation));
    }

    uint32_t extensionCount = 0;
    REQUIRE(api.vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr) == VK_SUCCESS);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    REQUIRE(
        api.vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data()) ==
        VK_SUCCESS
    );
    const auto hasExtension = [&](const char* name)
    {
        for (const auto& extension : extensions)
            if (std::strcmp(extension.extensionName, name) == 0)
                return true;
        return false;
    };

    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomicFloat = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT
    };
    VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT atomicFloat2 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_2_FEATURES_EXT
    };
    VkPhysicalDeviceShaderSubgroupRotateFeatures rotate = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_ROTATE_FEATURES
    };
    VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &atomicFloat;
    atomicFloat.pNext = &atomicFloat2;
    atomicFloat2.pNext = &rotate;
    api.vkGetPhysicalDeviceFeatures2(physicalDevice, &features);

    const bool hasAtomicFloat = hasExtension(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
    const bool hasAtomicFloat2 = hasAtomicFloat && hasExtension(VK_EXT_SHADER_ATOMIC_FLOAT_2_EXTENSION_NAME);
    const std::pair<bool, Capability> atomicCapabilities[] = {
        {hasAtomicFloat && atomicFloat.shaderBufferFloat32AtomicAdd, Capability::spvAtomicFloat32AddEXT},
        {hasAtomicFloat && atomicFloat.shaderBufferFloat64AtomicAdd, Capability::spvAtomicFloat64AddEXT},
        {hasAtomicFloat2 && atomicFloat2.shaderBufferFloat16AtomicAdd, Capability::spvAtomicFloat16AddEXT},
        {hasAtomicFloat2 && atomicFloat2.shaderBufferFloat16AtomicMinMax, Capability::spvAtomicFloat16MinMaxEXT},
        {hasAtomicFloat2 && atomicFloat2.shaderBufferFloat32AtomicMinMax, Capability::spvAtomicFloat32MinMaxEXT},
        {hasAtomicFloat2 && atomicFloat2.shaderBufferFloat64AtomicMinMax, Capability::spvAtomicFloat64MinMaxEXT},
    };
    for (const auto& [supported, capability] : atomicCapabilities)
    {
        const char* capabilityName = getRHI()->getCapabilityName(capability);
        CAPTURE(capabilityName);
        CHECK(device->hasCapability(capability) == supported);
    }
    CHECK(
        device->hasCapability(Capability::spvGroupNonUniformRotateKHR) ==
        (hasExtension(VK_KHR_SHADER_SUBGROUP_ROTATE_EXTENSION_NAME) && bool(rotate.shaderSubgroupRotate))
    );
    CHECK(
        device->hasCapability(Capability::spvGroupNonUniformPartitionedNV) ==
        (hasExtension(VK_NV_SHADER_SUBGROUP_PARTITIONED_EXTENSION_NAME) &&
         bool(subgroups.supportedOperations & VK_SUBGROUP_FEATURE_PARTITIONED_BIT_NV))
    );

    if (properties.properties.apiVersion >= VK_API_VERSION_1_2)
    {
        VkPhysicalDeviceVulkan12Features vulkan12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        features.pNext = &vulkan12;
        api.vkGetPhysicalDeviceFeatures2(physicalDevice, &features);
        CHECK(device->hasCapability(Capability::spvInt64Atomics) == bool(vulkan12.shaderBufferInt64Atomics));
        CHECK(device->hasCapability(Capability::spvPhysicalStorageBufferEXT) == bool(vulkan12.bufferDeviceAddress));
        CHECK(device->hasCapability(Capability::spvPhysicalStorageBufferKHR) == bool(vulkan12.bufferDeviceAddress));
        CHECK(device->hasCapability(Capability::spvVulkanMemoryModelKHR) == bool(vulkan12.vulkanMemoryModel));
        CHECK(
            device->hasCapability(Capability::spvVulkanMemoryModelDeviceScopeKHR) ==
            bool(vulkan12.vulkanMemoryModel && vulkan12.vulkanMemoryModelDeviceScope)
        );
    }
}
#endif

TEST_CASE("slang-capability-names")
{
    ComPtr<slang::IGlobalSession> session;
    REQUIRE_CALL(slang::createGlobalSession(session.writeRef()));
    // These names previously were missing from RHI or used a spelling Slang silently ignored.
    for (Capability capability : {
             Capability::spvGroupNonUniform,
             Capability::spvGroupNonUniformShuffleRelative,
             Capability::spvGroupNonUniformClustered,
             Capability::spvBFloat16KHR,
         })
    {
        const char* name = getRHI()->getCapabilityName(capability);
        CAPTURE(name);
        CHECK(session->findCapability(name) != SLANG_CAPABILITY_UNKNOWN);
    }
}

GPU_TEST_CASE("device-compute-dispatch-limits", CPU)
{
    REQUIRE(device);

    const DeviceLimits& limits = device->getInfo().limits;

    // A zero bound makes consumers that clamp dispatch sizes against it reject every
    // dispatch; the CPU backend must report a nonzero limit.
    CHECK(limits.maxComputeDispatchThreadGroups[0] > 0);
    CHECK(limits.maxComputeDispatchThreadGroups[1] > 0);
    CHECK(limits.maxComputeDispatchThreadGroups[2] > 0);
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
