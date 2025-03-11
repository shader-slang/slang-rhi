#include "testing.h"

#include "core/common.h"

#include "texture-test.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("texturetest-create", ALL & ~CUDA)
{
    TextureTestOptions options(device, 1);

    options.addVariants(
        TextureTestShapeFlags::S1D | TextureTestShapeFlags::S2D | TextureTestShapeFlags::S3D |
            TextureTestShapeFlags::SCube,
        TextureTestArrayFlags::ArrayBoth,
        TextureTestMipFlags::MipBoth,
        TextureTestMultisampleFlags::MultisampleBoth
    );

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            // stuff here
            fprintf(stderr, "Called\n");
        }
    );
}
