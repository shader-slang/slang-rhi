#include "testing.h"

#include "core/common.h"

#include "texture-test.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("texturetest-create", ALL)
{
    TextureTestOptions options(device, TextureInitMode::Random);
    options.addVariants(TTShape::All, TTArray::Both, TTMip::Both, TTMS::Both);


    runTextureTest(options, [](TextureTestContext* c) { c->getTextureData(0).checkEqual(c->getTexture(0)); });
}
