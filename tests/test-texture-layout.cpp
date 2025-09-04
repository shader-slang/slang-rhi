#include "testing.h"

#include "rhi-shared.h"

#include <string>
#include <map>
#include <functional>
#include <memory>


using namespace rhi;
using namespace rhi::testing;

void testTextureLayout(
    IDevice* device,
    ComPtr<ITexture> texture,
    uint32_t layer,
    uint32_t mip,
    SubresourceLayout expectedLayout
)
{
    SubresourceLayout layout;
    REQUIRE_CALL(texture->getSubresourceLayout(mip, &layout));

    CHECK_EQ(layout.size.width, expectedLayout.size.width);
    CHECK_EQ(layout.size.height, expectedLayout.size.height);
    CHECK_EQ(layout.size.depth, expectedLayout.size.depth);
    CHECK_EQ(layout.sizeInBytes, expectedLayout.sizeInBytes);
    CHECK_EQ(layout.colPitch, expectedLayout.colPitch);
    CHECK_EQ(layout.rowPitch, expectedLayout.rowPitch);
    CHECK_EQ(layout.slicePitch, expectedLayout.slicePitch);
}

void testTextureLayout2(
    IDevice* device,
    ComPtr<ITexture> texture,
    uint32_t layer,
    uint32_t mip,
    Offset3D offset,
    Extent3D extent,
    SubresourceLayout expectedLayout
)
{
    SubresourceLayout layout;
    REQUIRE_CALL(
        ((Texture*)texture.get())->getSubresourceRegionLayout(mip, offset, extent, kDefaultAlignment, &layout)
    );

    CHECK_EQ(layout.size.width, expectedLayout.size.width);
    CHECK_EQ(layout.size.height, expectedLayout.size.height);
    CHECK_EQ(layout.size.depth, expectedLayout.size.depth);
    CHECK_EQ(layout.sizeInBytes, expectedLayout.sizeInBytes);
    CHECK_EQ(layout.rowPitch, expectedLayout.rowPitch);
    CHECK_EQ(layout.slicePitch, expectedLayout.slicePitch);
}

int ALL_TEX = GpuTestFlags::ALL & ~GpuTestFlags::CPU;

GPU_TEST_CASE("texture-layout-1d-nomip", ALL_TEX)
{

    TextureDesc desc;
    desc.type = TextureType::Texture1D;
    desc.size = {256, 1, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = 1;
    desc.arrayLength = 1;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {{256, 1, 1}, 4, 1024, 1024, 1024});
}

// Checks layout adheres to the known 256B alignment of D3D12 and WGPU
GPU_TEST_CASE("texture-layout-1d-nomip-alignment", D3D12 | WGPU)
{

    TextureDesc desc;
    desc.type = TextureType::Texture1D;
    desc.size = {4, 1, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = 1;
    desc.arrayLength = 1;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {{4, 1, 1}, 4, 256, 256, 256});
}

// Metal doesn't support 1D textures with mip maps.
GPU_TEST_CASE("texture-layout-1d-mips", ALL_TEX & ~WGPU & ~Metal)
{

    TextureDesc desc;
    desc.type = TextureType::Texture1D;
    desc.size = {256, 1, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = kAllMips;
    desc.arrayLength = 1;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {{256, 1, 1}, 4, 1024, 1024, 1024});
    testTextureLayout(device, texture, 0, 1, {{128, 1, 1}, 4, 512, 512, 512});
}

GPU_TEST_CASE("texture-layout-1d-region", ALL_TEX)
{

    TextureDesc desc;
    desc.type = TextureType::Texture1D;
    desc.size = {256, 1, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = 1;
    desc.arrayLength = 1;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout2(device, texture, 0, 0, {16, 0, 0}, {64, 1, 1}, {{64, 1, 1}, 4, 256, 256, 256});
}

// Restrict to D3D12/WGPU as alignment needs accounting for
GPU_TEST_CASE("texture-layout-1d-region-rts", D3D12 | WGPU)
{

    TextureDesc desc;
    desc.type = TextureType::Texture1D;
    desc.size = {256, 1, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = 1;
    desc.arrayLength = 1;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout2(
        device,
        texture,
        0,
        0,
        {16, 0, 0},
        {kRemainingTextureSize, 1, 1},
        {{240, 1, 1}, 4, 1024, 1024, 1024}
    );
}

GPU_TEST_CASE("texture-layout-1darray-nomip", ALL_TEX & ~CUDA & ~WGPU)
{

    TextureDesc desc;
    desc.type = TextureType::Texture1DArray;
    desc.size = {256, 1, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = 1;
    desc.arrayLength = 4;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {{256, 1, 1}, 4, 1024, 1024, 1024});
    testTextureLayout(device, texture, 3, 0, {{256, 1, 1}, 4, 1024, 1024, 1024});
}

// Metal doesn't support 1D textures with mip maps.
GPU_TEST_CASE("texture-layout-1darray-mips", ALL_TEX & ~CUDA & ~WGPU & ~Metal)
{

    TextureDesc desc;
    desc.type = TextureType::Texture1DArray;
    desc.size = {256, 1, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = kAllMips;
    desc.arrayLength = 4;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {{256, 1, 1}, 4, 1024, 1024, 1024});
    testTextureLayout(device, texture, 0, 1, {{128, 1, 1}, 4, 512, 512, 512});
    testTextureLayout(device, texture, 3, 0, {{256, 1, 1}, 4, 1024, 1024, 1024});
    testTextureLayout(device, texture, 3, 1, {{128, 1, 1}, 4, 512, 512, 512});
}

GPU_TEST_CASE("texture-layout-2d-nomip", ALL_TEX)
{

    TextureDesc desc;
    desc.type = TextureType::Texture2D;
    desc.size = {256, 32, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = 1;
    desc.arrayLength = 1;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {{256, 32, 1}, 4, 1024, 1024 * 32, 1024 * 32});
}

GPU_TEST_CASE("texture-layout-2d-region", ALL_TEX)
{

    TextureDesc desc;
    desc.type = TextureType::Texture2D;
    desc.size = {256, 32, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = 1;
    desc.arrayLength = 1;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout2(device, texture, 0, 0, {16, 8, 0}, {64, 16, 1}, {{64, 16, 1}, 4, 256, 256 * 16, 256 * 16});
}

GPU_TEST_CASE("texture-layout-2d-mip", ALL_TEX)
{

    TextureDesc desc;
    desc.type = TextureType::Texture2D;
    desc.size = {256, 32, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = kAllMips;
    desc.arrayLength = 1;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {{256, 32, 1}, 4, 1024, 1024 * 32, 1024 * 32});
    testTextureLayout(device, texture, 0, 1, {{128, 16, 1}, 4, 512, 512 * 16, 512 * 16});
}

GPU_TEST_CASE("texture-layout-2d-array-nomip", ALL_TEX)
{

    TextureDesc desc;
    desc.type = TextureType::Texture2DArray;
    desc.size = {256, 32, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = 1;
    desc.arrayLength = 4;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {{256, 32, 1}, 4, 1024, 1024 * 32, 1024 * 32});
    testTextureLayout(device, texture, 3, 0, {{256, 32, 1}, 4, 1024, 1024 * 32, 1024 * 32});
}

GPU_TEST_CASE("texture-layout-2d-array-mips", ALL_TEX)
{

    TextureDesc desc;
    desc.type = TextureType::Texture2DArray;
    desc.size = {256, 32, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = kAllMips;
    desc.arrayLength = 4;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {{256, 32, 1}, 4, 1024, 1024 * 32, 1024 * 32});
    testTextureLayout(device, texture, 0, 1, {{128, 16, 1}, 4, 512, 512 * 16, 512 * 16});
    testTextureLayout(device, texture, 3, 0, {{256, 32, 1}, 4, 1024, 1024 * 32, 1024 * 32});
    testTextureLayout(device, texture, 3, 1, {{128, 16, 1}, 4, 512, 512 * 16, 512 * 16});
}

GPU_TEST_CASE("texture-layout-3d-nomip", ALL_TEX)
{

    TextureDesc desc;
    desc.type = TextureType::Texture3D;
    desc.size = {256, 32, 16};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = 1;
    desc.arrayLength = 1;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {{256, 32, 16}, 4, 1024, 1024 * 32, 1024 * 32 * 16});
}

GPU_TEST_CASE("texture-layout-3d-region", ALL_TEX)
{

    TextureDesc desc;
    desc.type = TextureType::Texture3D;
    desc.size = {256, 32, 16};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = 1;
    desc.arrayLength = 1;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout2(device, texture, 0, 0, {16, 8, 4}, {64, 16, 8}, {{64, 16, 8}, 4, 256, 256 * 16, 256 * 16 * 8});
}


GPU_TEST_CASE("texture-layout-3d-mip", ALL_TEX)
{

    TextureDesc desc;
    desc.type = TextureType::Texture3D;
    desc.size = {256, 32, 16};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = kAllMips;
    desc.arrayLength = 1;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {{256, 32, 16}, 4, 1024, 1024 * 32, 1024 * 32 * 16});
    testTextureLayout(device, texture, 0, 1, {{128, 16, 8}, 4, 512, 512 * 16, 512 * 16 * 8});
}

GPU_TEST_CASE("texture-layout-cube-nomip", ALL_TEX)
{

    TextureDesc desc;
    desc.type = TextureType::TextureCube;
    desc.size = {256, 256, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = 1;
    desc.arrayLength = 1;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {{256, 256, 1}, 4, 1024, 1024 * 256, 1024 * 256});
}

GPU_TEST_CASE("texture-layout-cube-mip", ALL_TEX)
{

    TextureDesc desc;
    desc.type = TextureType::TextureCube;
    desc.size = {256, 256, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = kAllMips;
    desc.arrayLength = 1;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {{256, 256, 1}, 4, 1024, 1024 * 256, 1024 * 256});
    testTextureLayout(device, texture, 0, 1, {{128, 128, 1}, 4, 512, 512 * 128, 512 * 128});
}

GPU_TEST_CASE("texture-layout-cube-array-nomip", ALL_TEX)
{

    TextureDesc desc;
    desc.type = TextureType::TextureCubeArray;
    desc.size = {256, 256, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = 1;
    desc.arrayLength = 4;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {{256, 256, 1}, 4, 1024, 1024 * 256, 1024 * 256});
    testTextureLayout(device, texture, 3 * 6, 0, {{256, 256, 1}, 4, 1024, 1024 * 256, 1024 * 256});
}

GPU_TEST_CASE("texture-layout-cube-array-mips", ALL_TEX)
{

    TextureDesc desc;
    desc.type = TextureType::TextureCubeArray;
    desc.size = {256, 256, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = kAllMips;
    desc.arrayLength = 4;
    desc.usage = TextureUsage::ShaderResource;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {{256, 256, 1}, 4, 1024, 1024 * 256, 1024 * 256});
    testTextureLayout(device, texture, 0, 1, {{128, 128, 1}, 4, 512, 512 * 128, 512 * 128});
    testTextureLayout(device, texture, 3 * 6, 0, {{256, 256, 1}, 4, 1024, 1024 * 256, 1024 * 256});
    testTextureLayout(device, texture, 3 * 6, 1, {{128, 128, 1}, 4, 512, 512 * 128, 512 * 128});
}
