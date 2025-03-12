#include "testing.h"

#include "core/common.h"

#include "texture-test.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("texturetest-create", ALL)
{
    TextureTestOptions options(device, 1);
    options.addVariants(TTShape::All, TTArray::Both, TTMip::Both, TTMS::Both);


    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            // read-back not implemented
            if (c->getDevice()->getDeviceType() == DeviceType::CPU ||
                c->getDevice()->getDeviceType() == DeviceType::D3D11)
                return;

            c->getTextureData(0).checkEqual(c->getTexture(0));
        }
    );
}
