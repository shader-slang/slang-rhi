#include "testing.h"
#include "texture-test.h"

#include <string>
#include <map>
#include <functional>
#include <memory>

using namespace rhi;
using namespace rhi::testing;

static const std::map<Format, float> kFormatFloatEpsilon = {
    {Format::R8Unorm, 2.f * 0.6f / 255.f},
    {Format::R8Snorm, 2.f * 0.6f / 127.f},
    {Format::RG8Unorm, 2.f * 0.6f / 255.f},
    {Format::RG8Snorm, 2.f * 0.6f / 127.f},
    {Format::RGBA8Unorm, 2.f * 0.6f / 255.f},
    {Format::RGBA8Snorm, 2.f * 0.6f / 127.f},
    {Format::BGRA8Unorm, 2.f * 0.6f / 255.f},
    {Format::BGRX8Unorm, 2.f * 0.6f / 255.f},
    {Format::R16Unorm, 2.f * 0.6f / 65535.f},
    {Format::R16Snorm, 2.f * 0.6f / 32767.f},
    {Format::RG16Unorm, 2.f * 0.6f / 65535.f},
    {Format::RG16Snorm, 2.f * 0.6f / 32767.f},
    {Format::RGBA16Unorm, 2.f * 0.6f / 65535.f},
    {Format::RGBA16Snorm, 2.f * 0.6f / 32767.f},
    {Format::BGRA4Unorm, 2.f * 0.6f / 15.f},
    {Format::B5G6R5Unorm, 2.f * 0.6f / 31.f},
};

inline float getFormatFloatEpsilon(Format format)
{
    auto it = kFormatFloatEpsilon.find(format);
    return it != kFormatFloatEpsilon.end() ? it->second : 0.f;
}

static const std::vector<Format> kFloatFormats = {
    // Floating point formats
    Format::R16Float,
    Format::RG16Float,
    Format::RGBA16Float,
    Format::R32Float,
    Format::RG32Float,
    // Format::RGB32Float, // TODO D3D11/D3D12 doesn't support UAVs on RGB32Float
    Format::RGBA32Float,
    // Format::R11G11B10Float, // TODO format conversion not supported yet

    // Unsigned normalized formats
    Format::R8Unorm,
    Format::RG8Unorm,
    Format::RGBA8Unorm,
    // Format::RGBA8UnormSrgb,
    Format::BGRA8Unorm,
    // Format::BGRA8UnormSrgb,
    // Format::BGRX8Unorm,
    // Format::BGRX8UnormSrgb,
    Format::R16Unorm,
    Format::RG16Unorm,
    Format::RGBA16Unorm,
    // Format::BGRA4Unorm,
    // Format::B5G6R5Unorm,
    // Format::BGR5A1Unorm,
    // Format::RGB10A2Unorm,

    // Signed normalized formats
    Format::R8Snorm,
    Format::RG8Snorm,
    Format::RGBA8Snorm,
    Format::R16Snorm,
    Format::RG16Snorm,
    Format::RGBA16Snorm,
};

static const std::vector<Format> kUintFormats = {
    Format::R8Uint,
    Format::RG8Uint,
    Format::RGBA8Uint,
    Format::R16Uint,
    Format::RG16Uint,
    Format::RGBA16Uint,
    Format::R32Uint,
    Format::RG32Uint,
    // Format::RGB32Uint, // TODO D3D11/D3D12 doesn't support UAVs on RGB32Uint
    Format::RGBA32Uint,
    // Format::R64Uint, // TODO not supported yet
    // Format::RGB10A2Uint, // TODO not supported yet
};

static const std::vector<Format> kSintFormats = {
    Format::R8Sint,
    Format::RG8Sint,
    Format::RGBA8Sint,
    Format::R16Sint,
    Format::RG16Sint,
    Format::RGBA16Sint,
    Format::R32Sint,
    Format::RG32Sint,
    // Format::RGB32Sint, // TODO D3D11/D3D12 doesn't support UAVs on RGB32Sint
    Format::RGBA32Sint,
    // Format::R64Sint, // TODO not supported yet
};

static const std::vector<Format> kDepthStencilFormats = {
    Format::D16Unorm,
    Format::D32Float,
    Format::D32FloatS8Uint,
};

GPU_TEST_CASE("cmd-clear-texture-float-zero", D3D11 | D3D12 | Vulkan | Metal | CUDA)
{
    TextureTestOptions options(device, 1);
    options
        .addVariants(TTShape::All, TTArray::Both, TTMip::Both, TTMS::Off, kFloatFormats, TextureUsage::UnorderedAccess);

    runTextureTest(
        options,
        [device](TextureTestContext* c)
        {
            ComPtr<ITexture> texture = c->getTexture();
            // D3D11 doesn't support UAVs on cube textures
            if (device->getDeviceType() == DeviceType::D3D11 &&
                (texture->getDesc().type == TextureType::TextureCube ||
                 texture->getDesc().type == TextureType::TextureCubeArray))
                return;
            ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
            {
                ComPtr<ICommandEncoder> encoder = queue->createCommandEncoder();
                float clearValue[4] = {0.f, 0.f, 0.f, 0.f};
                encoder->clearTextureFloat(texture, kEntireTexture, clearValue);
                queue->submit(encoder->finish());
                c->getTextureData().clearFloat(clearValue);
                c->getTextureData().checkEqualFloat(texture);
            }
        }
    );
}

GPU_TEST_CASE("cmd-clear-texture-float-pattern", D3D11 | D3D12 | Vulkan | Metal | CUDA)
{
    TextureTestOptions options(device, 1);
    options
        .addVariants(TTShape::All, TTArray::Both, TTMip::Both, TTMS::Off, kFloatFormats, TextureUsage::UnorderedAccess);

    runTextureTest(
        options,
        [device](TextureTestContext* c)
        {
            ComPtr<ITexture> texture = c->getTexture();
            // D3D11 doesn't support UAVs on cube textures
            if (device->getDeviceType() == DeviceType::D3D11 &&
                (texture->getDesc().type == TextureType::TextureCube ||
                 texture->getDesc().type == TextureType::TextureCubeArray))
                return;
            ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
            {
                ComPtr<ICommandEncoder> encoder = queue->createCommandEncoder();
                float clearValue[4] = {0.25f, 0.5f, 0.75f, 1.f};
                encoder->clearTextureFloat(texture, kEntireTexture, clearValue);
                queue->submit(encoder->finish());
                c->getTextureData().clearFloat(clearValue);
                c->getTextureData().checkEqualFloat(texture, getFormatFloatEpsilon(c->getTextureData().desc.format));
            }
        }
    );
}

GPU_TEST_CASE("cmd-clear-texture-uint-zero", D3D11 | D3D12 | Vulkan | Metal | CUDA)
{
    TextureTestOptions options(device, 1);
    options
        .addVariants(TTShape::All, TTArray::Both, TTMip::Both, TTMS::Off, kUintFormats, TextureUsage::UnorderedAccess);

    runTextureTest(
        options,
        [device](TextureTestContext* c)
        {
            ComPtr<ITexture> texture = c->getTexture();
            // D3D11 doesn't support UAVs on cube textures
            if (device->getDeviceType() == DeviceType::D3D11 &&
                (texture->getDesc().type == TextureType::TextureCube ||
                 texture->getDesc().type == TextureType::TextureCubeArray))
                return;
            ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
            {
                ComPtr<ICommandEncoder> encoder = queue->createCommandEncoder();
                uint32_t clearValue[4] = {0, 0, 0, 0};
                encoder->clearTextureUint(texture, kEntireTexture, clearValue);
                queue->submit(encoder->finish());
                c->getTextureData().clearUint(clearValue);
                c->getTextureData().checkEqual(texture);
            }
        }
    );
}

GPU_TEST_CASE("cmd-clear-texture-uint-pattern", D3D11 | D3D12 | Vulkan | Metal | CUDA)
{
    TextureTestOptions options(device, 1);
    options
        .addVariants(TTShape::All, TTArray::Both, TTMip::Both, TTMS::Off, kUintFormats, TextureUsage::UnorderedAccess);

    runTextureTest(
        options,
        [device](TextureTestContext* c)
        {
            ComPtr<ITexture> texture = c->getTexture();
            // D3D11 doesn't support UAVs on cube textures
            if (device->getDeviceType() == DeviceType::D3D11 &&
                (texture->getDesc().type == TextureType::TextureCube ||
                 texture->getDesc().type == TextureType::TextureCubeArray))
                return;
            ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
            {
                ComPtr<ICommandEncoder> encoder = queue->createCommandEncoder();
                uint32_t clearValue[4] = {10, 100, 1000, 10000};
                encoder->clearTextureUint(texture, kEntireTexture, clearValue);
                queue->submit(encoder->finish());
                c->getTextureData().clearUint(clearValue);
                c->getTextureData().checkEqual(texture);
            }
        }
    );
}

GPU_TEST_CASE("cmd-clear-texture-sint-zero", D3D11 | D3D12 | Vulkan | Metal | CUDA)
{
    TextureTestOptions options(device, 1);
    options
        .addVariants(TTShape::All, TTArray::Both, TTMip::Both, TTMS::Off, kSintFormats, TextureUsage::UnorderedAccess);

    runTextureTest(
        options,
        [device](TextureTestContext* c)
        {
            ComPtr<ITexture> texture = c->getTexture();
            // D3D11 doesn't support UAVs on cube textures
            if (device->getDeviceType() == DeviceType::D3D11 &&
                (texture->getDesc().type == TextureType::TextureCube ||
                 texture->getDesc().type == TextureType::TextureCubeArray))
                return;
            ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
            {
                ComPtr<ICommandEncoder> encoder = queue->createCommandEncoder();
                int32_t clearValue[4] = {0, 0, 0, 0};
                encoder->clearTextureSint(texture, kEntireTexture, clearValue);
                queue->submit(encoder->finish());
                c->getTextureData().clearSint(clearValue);
                c->getTextureData().checkEqual(texture);
            }
        }
    );
}

GPU_TEST_CASE("cmd-clear-texture-sint-pattern", D3D11 | D3D12 | Vulkan | Metal | CUDA)
{
    TextureTestOptions options(device, 1);
    options
        .addVariants(TTShape::All, TTArray::Both, TTMip::Both, TTMS::Off, kSintFormats, TextureUsage::UnorderedAccess);

    runTextureTest(
        options,
        [device](TextureTestContext* c)
        {
            ComPtr<ITexture> texture = c->getTexture();
            // D3D11 doesn't support UAVs on cube textures
            if (device->getDeviceType() == DeviceType::D3D11 &&
                (texture->getDesc().type == TextureType::TextureCube ||
                 texture->getDesc().type == TextureType::TextureCubeArray))
                return;
            ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
            {
                ComPtr<ICommandEncoder> encoder = queue->createCommandEncoder();
                int32_t clearValue[4] = {-100, -10, 10, 100};
                encoder->clearTextureSint(texture, kEntireTexture, clearValue);
                queue->submit(encoder->finish());
                c->getTextureData().clearSint(clearValue);
                c->getTextureData().checkEqual(texture);
            }
        }
    );
}

GPU_TEST_CASE("cmd-clear-texture-depth-stencil", D3D11 | D3D12 | Vulkan | Metal)
{
#if 0
    TextureTestOptions options(device, 1);
    options.addVariants(
        TTShape::D2,
        TTArray::Both,
        TTMip::Both,
        TTMS::Off,
        kDepthStencilFormats,
        TextureUsage::DepthStencil
    );

    runTextureTest(
        options,
        [device](TextureTestContext* c)
        {
            printf("cmd-clear-texture-depth-stencil\n");
            ComPtr<ITexture> texture = c->getTexture();
            ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
            {
                ComPtr<ICommandEncoder> encoder = queue->createCommandEncoder();

                // Clear depth to 0.5 and stencil to 42
                float depthValue = 0.5f;
                uint8_t stencilValue = 42;

                // Create a subresource range that covers all mips and array layers
                SubresourceRange range = kEntireTexture;

                // Clear both depth and stencil
                encoder->clearTextureDepthStencil(texture, range, true, depthValue, true, stencilValue);
                queue->submit(encoder->finish());

                // Verify the clear values
                // For depth formats, we use clearFloat with the depth value
                float clearValues[4] = {depthValue, 0.0f, 0.0f, 0.0f};
                c->getTextureData().clearFloat(clearValues);
                c->getTextureData().checkEqual(texture);
            }
        }
    );
#endif
}
