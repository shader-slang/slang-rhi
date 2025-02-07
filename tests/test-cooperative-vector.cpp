#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

void testCooperativeVectorProperties(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    if (!device->hasFeature("cooperative-vector"))
        SKIP("cooperative-vector not supported");

    uint32_t propertyCount = 0;
    REQUIRE_CALL(device->getCooperativeVectorProperties(nullptr, &propertyCount));
    std::vector<CooperativeVectorProperties> properties(propertyCount);
    REQUIRE_CALL(device->getCooperativeVectorProperties(properties.data(), &propertyCount));

    CHECK(propertyCount > 0);
}

TEST_CASE("cooperative-vector-properties")
{
    runGpuTests(
        testCooperativeVectorProperties,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
        }
    );
}
