#include "testing.h"

#include "core/common.h"

#include "texture-test.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("texture-create", ALL)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::All, TTArray::Both, TTMip::Both, TTMS::Both);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            const TextureData& data = c->getTextureData();

            // If texture type couldn't be initialized (eg multisampled or multi-aspect)
            // then don't check it's contents.
            if (data.initMode == TextureInitMode::None)
                return;

            data.checkEqual(c->getTexture());
        }
    );
}
