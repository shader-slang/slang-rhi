#include "testing.h"
#include "core/common.h"

using namespace rhi;
using namespace rhi::testing;

TEST_CASE("floatToHalf")
{
    // Test cases from https://en.wikipedia.org/wiki/Half-precision_floating-point_format#Conversions
    CHECK_EQ(math::floatToHalf(0.0f), 0x0000);
    CHECK_EQ(math::floatToHalf(-0.0f), 0x8000);
    CHECK_EQ(math::floatToHalf(1.0f), 0x3c00);
    CHECK_EQ(math::floatToHalf(-1.0f), 0xbc00);
    CHECK_EQ(math::floatToHalf(65504.0f), 0x7bff);
}

TEST_CASE("halfToFloat")
{
    // Test cases from https://en.wikipedia.org/wiki/Half-precision_floating-point_format#Conversions
    CHECK_EQ(math::halfToFloat(0x0000), 0.0f);
    CHECK_EQ(math::halfToFloat(0x8000), -0.0f);
    CHECK_EQ(math::halfToFloat(0x3c00), 1.0f);
    CHECK_EQ(math::halfToFloat(0xbc00), -1.0f);
    CHECK_EQ(math::halfToFloat(0x7bff), 65504.0f);
}
