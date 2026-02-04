#include "testing.h"
#include "core/common.h"

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
