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
            const TextureData& data = c->getTextureData(0);

            // If texture type couldn't be initialized (eg multisampled or multi-aspect)
            // then don't check it's contents.
            if (data.initMode == TextureInitMode::None)
                return;

            data.checkEqual(c->getTexture(0));
        }
    );
}
