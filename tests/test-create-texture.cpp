#include "testing.h"

#include "core/common.h"

#include "texture-test.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("texture-create", ALL)
{
    TextureTestOptions options(device);
    options.addVariants(
        TTShape::All,    // all shapes
        TTArray::Both,   // array and non-array
        TTMip::Both,     // with/without mips
        TTMS::Both,      // with/without multisampling (when available)
        TTPowerOf2::Both // test both power-of-2 and non-power-of-2 sizes where possible
    );

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            const TextureData& data = c->getTextureData();

            // Enable this to helpfully log all created textures.
            // fprintf(stderr, "Created texture %s\n", c->getTexture()->getDesc().label);

            // If texture type couldn't be initialized (eg multisampled or multi-aspect)
            // then don't check it's contents.
            if (data.initMode == TextureInitMode::None)
                return;

            data.checkEqual(c->getTexture());
        }
    );
}
