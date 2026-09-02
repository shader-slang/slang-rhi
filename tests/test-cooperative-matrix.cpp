#include "testing.h"
#include "core/common.h"

#if SLANG_RHI_ENABLE_VULKAN
#include "vulkan/vk-utils.h"
#endif

#include <algorithm>
#include <iterator>
#include <ostream>

using namespace rhi;
using namespace rhi::testing;

static CooperativeMatrixDesc makeBasicCoopMatDesc()
{
    CooperativeMatrixDesc desc = {};
    desc.m = 16;
    desc.n = 16;
    desc.k = 16;
    desc.aType = CooperativeMatrixComponentType::Float16;
    desc.bType = CooperativeMatrixComponentType::Float16;
    desc.cType = CooperativeMatrixComponentType::Float16;
    desc.resultType = CooperativeMatrixComponentType::Float16;
    desc.scope = CooperativeMatrixScope::Subgroup;
    return desc;
}

TEST_CASE("cooperative-matrix-2-feature-names")
{
    struct TestCase
    {
        Feature feature;
        const char* name;
    };
    const TestCase testCases[] = {
        {Feature::CooperativeMatrixReductions, "cooperative-matrix-reductions"},
        {Feature::CooperativeMatrixConversions, "cooperative-matrix-conversions"},
        {Feature::CooperativeMatrixPerElementOperations, "cooperative-matrix-per-element-operations"},
        {Feature::CooperativeMatrixTensorAddressing, "cooperative-matrix-tensor-addressing"},
        {Feature::CooperativeMatrixBlockLoads, "cooperative-matrix-block-loads"},
    };

    for (const auto& testCase : testCases)
    {
        CHECK(std::string_view(getRHI()->getFeatureName(testCase.feature)) == testCase.name);
    }
}

#if SLANG_RHI_ENABLE_VULKAN
TEST_CASE("cooperative-matrix-2-subfeature-projection")
{
    const auto featureBits = {
        &VkPhysicalDeviceCooperativeMatrix2FeaturesNV::cooperativeMatrixWorkgroupScope,
        &VkPhysicalDeviceCooperativeMatrix2FeaturesNV::cooperativeMatrixFlexibleDimensions,
        &VkPhysicalDeviceCooperativeMatrix2FeaturesNV::cooperativeMatrixReductions,
        &VkPhysicalDeviceCooperativeMatrix2FeaturesNV::cooperativeMatrixConversions,
        &VkPhysicalDeviceCooperativeMatrix2FeaturesNV::cooperativeMatrixPerElementOperations,
        &VkPhysicalDeviceCooperativeMatrix2FeaturesNV::cooperativeMatrixTensorAddressing,
        &VkPhysicalDeviceCooperativeMatrix2FeaturesNV::cooperativeMatrixBlockLoads,
    };
    for (const auto featureBit : featureBits)
    {
        VkPhysicalDeviceCooperativeMatrix2FeaturesNV vulkanFeatures = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_2_FEATURES_NV
        };
        vulkanFeatures.*featureBit = VK_TRUE;
        CHECK(vk::hasAnyCooperativeMatrix2Feature(vulkanFeatures));

        auto extensionSelection = vk::selectCooperativeMatrix2Extensions(true, true, false, vulkanFeatures);
        CHECK(extensionSelection.enableCooperativeMatrix2Extension);
        CHECK(extensionSelection.appendCooperativeMatrixExtensionDependency);

        extensionSelection = vk::selectCooperativeMatrix2Extensions(true, true, true, vulkanFeatures);
        CHECK(extensionSelection.enableCooperativeMatrix2Extension);
        CHECK_FALSE(extensionSelection.appendCooperativeMatrixExtensionDependency);

        extensionSelection = vk::selectCooperativeMatrix2Extensions(false, true, false, vulkanFeatures);
        CHECK_FALSE(extensionSelection.enableCooperativeMatrix2Extension);
        CHECK_FALSE(extensionSelection.appendCooperativeMatrixExtensionDependency);

        extensionSelection = vk::selectCooperativeMatrix2Extensions(false, true, true, vulkanFeatures);
        CHECK_FALSE(extensionSelection.enableCooperativeMatrix2Extension);
        CHECK_FALSE(extensionSelection.appendCooperativeMatrixExtensionDependency);

        extensionSelection = vk::selectCooperativeMatrix2Extensions(true, false, false, vulkanFeatures);
        CHECK_FALSE(extensionSelection.enableCooperativeMatrix2Extension);
        CHECK_FALSE(extensionSelection.appendCooperativeMatrixExtensionDependency);
    }

    VkPhysicalDeviceCooperativeMatrix2FeaturesNV noFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_2_FEATURES_NV
    };
    CHECK_FALSE(vk::hasAnyCooperativeMatrix2Feature(noFeatures));
    const auto extensionSelection = vk::selectCooperativeMatrix2Extensions(true, true, false, noFeatures);
    CHECK_FALSE(extensionSelection.enableCooperativeMatrix2Extension);
    CHECK_FALSE(extensionSelection.appendCooperativeMatrixExtensionDependency);

    struct TestCase
    {
        VkBool32 VkPhysicalDeviceCooperativeMatrix2FeaturesNV::* featureBit;
        Feature feature;
        Capability capabilities[2];
        size_t capabilityCount;
    };
    const TestCase testCases[] = {
        {
            &VkPhysicalDeviceCooperativeMatrix2FeaturesNV::cooperativeMatrixReductions,
            Feature::CooperativeMatrixReductions,
            {Capability::spvCooperativeMatrixReductionsNV},
            1,
        },
        {
            &VkPhysicalDeviceCooperativeMatrix2FeaturesNV::cooperativeMatrixConversions,
            Feature::CooperativeMatrixConversions,
            {Capability::spvCooperativeMatrixConversionsNV},
            1,
        },
        {
            &VkPhysicalDeviceCooperativeMatrix2FeaturesNV::cooperativeMatrixPerElementOperations,
            Feature::CooperativeMatrixPerElementOperations,
            {Capability::spvCooperativeMatrixPerElementOperationsNV},
            1,
        },
        {
            &VkPhysicalDeviceCooperativeMatrix2FeaturesNV::cooperativeMatrixTensorAddressing,
            Feature::CooperativeMatrixTensorAddressing,
            {Capability::spvCooperativeMatrixTensorAddressingNV, Capability::spvTensorAddressingNV},
            2,
        },
        {
            &VkPhysicalDeviceCooperativeMatrix2FeaturesNV::cooperativeMatrixBlockLoads,
            Feature::CooperativeMatrixBlockLoads,
            {Capability::spvCooperativeMatrixBlockLoadsNV},
            1,
        },
    };

    for (const auto& testCase : testCases)
    {
        VkPhysicalDeviceCooperativeMatrix2FeaturesNV vulkanFeatures = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_2_FEATURES_NV
        };
        vulkanFeatures.*testCase.featureBit = VK_TRUE;
        std::vector<Feature> features;
        std::vector<Capability> capabilities;

        vk::appendCooperativeMatrix2Subfeatures(vulkanFeatures, features, capabilities);

        REQUIRE(features.size() == 1);
        CHECK(features[0] == testCase.feature);
        REQUIRE(capabilities.size() == testCase.capabilityCount);
        for (size_t i = 0; i < testCase.capabilityCount; ++i)
        {
            CHECK(capabilities[i] == testCase.capabilities[i]);
        }
    }

    std::vector<Feature> features;
    std::vector<Capability> capabilities;
    vk::appendCooperativeMatrix2Subfeatures(noFeatures, features, capabilities);
    CHECK(features.empty());
    CHECK(capabilities.empty());

    VkPhysicalDeviceCooperativeMatrix2FeaturesNV allFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_2_FEATURES_NV
    };
    for (const auto featureBit : featureBits)
    {
        allFeatures.*featureBit = VK_TRUE;
    }
    CHECK(vk::hasAnyCooperativeMatrix2Feature(allFeatures));
    vk::appendCooperativeMatrix2Subfeatures(allFeatures, features, capabilities);
    CHECK(features.size() == std::size(testCases));
    CHECK(capabilities.size() == std::size(testCases) + 1);
    for (const auto& testCase : testCases)
    {
        CHECK(std::find(features.begin(), features.end(), testCase.feature) != features.end());
        for (size_t i = 0; i < testCase.capabilityCount; ++i)
        {
            CHECK(std::find(capabilities.begin(), capabilities.end(), testCase.capabilities[i]) != capabilities.end());
        }
    }
}
#endif

GPU_TEST_CASE("cooperative-matrix-invalid-desc", ALL)
{
    CooperativeMatrixDesc desc = makeBasicCoopMatDesc();
    desc.m = 0;
    desc.n = 0;
    desc.k = 0;

    bool supported = true;
    REQUIRE_CALL(device->isCooperativeMatrixSupported(desc, &supported));
    CHECK_FALSE(supported);
}

GPU_TEST_CASE("cooperative-matrix-query", Vulkan)
{
    CooperativeMatrixDesc desc = makeBasicCoopMatDesc();

    bool supported = false;
    REQUIRE_CALL(device->isCooperativeMatrixSupported(desc, &supported));
    if (!device->hasFeature(Feature::CooperativeMatrix))
    {
        CHECK_FALSE(supported);
        return;
    }
    bool anySupported = false;
    const uint32_t sizes[] = {16, 32, 64};
    const CooperativeMatrixScope scopes[] = {
        CooperativeMatrixScope::Subgroup,
        CooperativeMatrixScope::Workgroup,
    };
    const CooperativeMatrixComponentType types[] = {
        CooperativeMatrixComponentType::Float16,
        CooperativeMatrixComponentType::Bfloat16,
        CooperativeMatrixComponentType::FloatE4M3,
    };
    for (CooperativeMatrixComponentType type : types)
    {
        for (CooperativeMatrixScope scope : scopes)
        {
            for (uint32_t m : sizes)
            {
                for (uint32_t n : sizes)
                {
                    for (uint32_t k : sizes)
                    {
                        CooperativeMatrixDesc sweepDesc = {};
                        sweepDesc.m = m;
                        sweepDesc.n = n;
                        sweepDesc.k = k;
                        sweepDesc.aType = type;
                        sweepDesc.bType = type;
                        sweepDesc.cType = type;
                        sweepDesc.resultType = type;
                        sweepDesc.scope = scope;

                        bool sweepSupported = false;
                        REQUIRE_CALL(device->isCooperativeMatrixSupported(sweepDesc, &sweepSupported));
                        if (sweepSupported)
                        {
                            anySupported = true;
                            break;
                        }
                    }
                    if (anySupported)
                        break;
                }
                if (anySupported)
                    break;
            }
            if (anySupported)
                break;
        }
        if (anySupported)
            break;
    }
    CHECK(anySupported);

    CooperativeMatrixDesc workgroupDesc = desc;
    workgroupDesc.scope = CooperativeMatrixScope::Workgroup;
    bool supportedWorkgroup = false;
    REQUIRE_CALL(device->isCooperativeMatrixSupported(workgroupDesc, &supportedWorkgroup));
    if (device->hasFeature(Feature::CooperativeMatrix2))
    {
        CHECK(supportedWorkgroup);
    }
}

GPU_TEST_CASE("cooperative-matrix-2-feature-capabilities", Vulkan)
{
    struct TestCase
    {
        Feature feature;
        const char* name;
        Capability capability;
    };
    const TestCase testCases[] = {
        {
            Feature::CooperativeMatrixReductions,
            "cooperative-matrix-reductions",
            Capability::spvCooperativeMatrixReductionsNV,
        },
        {
            Feature::CooperativeMatrixConversions,
            "cooperative-matrix-conversions",
            Capability::spvCooperativeMatrixConversionsNV,
        },
        {
            Feature::CooperativeMatrixPerElementOperations,
            "cooperative-matrix-per-element-operations",
            Capability::spvCooperativeMatrixPerElementOperationsNV,
        },
        {
            Feature::CooperativeMatrixTensorAddressing,
            "cooperative-matrix-tensor-addressing",
            Capability::spvCooperativeMatrixTensorAddressingNV,
        },
        {
            Feature::CooperativeMatrixBlockLoads,
            "cooperative-matrix-block-loads",
            Capability::spvCooperativeMatrixBlockLoadsNV,
        },
    };

    for (const auto& testCase : testCases)
    {
        const bool supported = device->hasFeature(testCase.feature);
        CHECK(device->hasFeature(testCase.name) == supported);
        CHECK(device->hasCapability(testCase.capability) == supported);
        if (supported)
        {
            CHECK(device->hasCapability(Capability::SPV_NV_cooperative_matrix2));
        }
    }

    CHECK(
        device->hasCapability(Capability::spvTensorAddressingNV) ==
        device->hasFeature(Feature::CooperativeMatrixTensorAddressing)
    );
}
