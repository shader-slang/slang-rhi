#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

TEST_CASE("blob")
{
    SUBCASE("without data")
    {
        ComPtr<ISlangBlob> blob;
        REQUIRE_CALL(getRHI()->createBlob(nullptr, 64, blob.writeRef()));
        CHECK(blob != nullptr);
        CHECK(blob->getBufferSize() == 64);
        CHECK(blob->getBufferPointer() != nullptr);
    }
    SUBCASE("with data")
    {
        ComPtr<ISlangBlob> blob;
        const char* data = "Hello, World!";
        REQUIRE_CALL(getRHI()->createBlob(data, 14, blob.writeRef()));
        CHECK(blob != nullptr);
        CHECK(blob->getBufferSize() == 14);
        CHECK(blob->getBufferPointer() != nullptr);
        CHECK(std::memcmp(blob->getBufferPointer(), data, 14) == 0);
    }
}
