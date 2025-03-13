#include "testing.h"

#include "core/common.h"

#include "texture-test.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("texturetest-create", ALL)
{
    TextureTestOptions options(device, TextureInitMode::Random);
    options.addVariants(TTShape::All, TTArray::Both, TTMip::Both, TTMS::Both);
    // options.addVariants(TTShape::All, TTArray::Off, TTMip::Off, TTMS::On);


    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            // Can't read multisampled textures.
            if (isMultisamplingType(c->getTexture(0)->getDesc().type))
                return;

            c->getTextureData(0).checkEqual(c->getTexture(0));
        }
    );
}
