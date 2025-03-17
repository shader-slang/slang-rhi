#include "testing.h"

#include "texture-utils.h"
#include "texture-test.h"

#include "core/common.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("cmd-copy-texture-full", D3D12 | Vulkan | Metal | CUDA | WGPU)
{
    TextureTestOptions options(device);
    options.addVariants(TTShape::All, TTArray::Both, TTMip::Both, TTMS::Both);

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data.
            TextureData& data = c->getTextureData();

            // Create a new, uninitialized texture with same descriptor
            TextureData newData;
            newData.init(device, data.desc, TextureInitMode::None);
            ComPtr<ITexture> newTexture;
            REQUIRE_CALL(newData.createTexture(newTexture.writeRef()));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Copy all subresources with offsets at 0 and size of whole texture.
            commandEncoder->copyTexture(
                newTexture,
                {0, 0, 0, 0},
                {0, 0, 0},
                c->getTexture(),
                {0, 0, 0, 0},
                {0, 0, 0},
                Extents::kWholeTexture
            );
            queue->submit(commandEncoder->finish());

            // Can't read-back ms or depth/stencil formats
            if (isMultisamplingType(data.desc.type))
                return;
            if (data.formatInfo.hasDepth && data.formatInfo.hasStencil)
                return;

            // Verify it uploaded correctly
            data.checkEqual(newTexture);
        }
    );
}
