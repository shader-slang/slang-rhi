#include "testing.h"

#include <string>
#include <map>
#include <functional>
#include <memory>

using namespace rhi;
using namespace rhi::testing;

struct TestOpts
{
    TextureDesc textureDesc;
    SubresourceRange subresourceRange;
};

void testReadTextureToBuffer(const TestOpts& opts) {}
