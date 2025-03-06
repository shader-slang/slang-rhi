#include "testing.h"

#include <string>
#include <map>
#include <functional>
#include <memory>

using namespace rhi;
using namespace rhi::testing;

void testTextureLayout(
    IDevice* device,
    ComPtr<ITexture> texture,
    size_t layerIndex,
    size_t mipLevel,
    SubresourceLayout expectedLayout
)
{
    SubresourceLayout layout;
    REQUIRE_CALL(texture->getSubresourceLayout(mipLevel, layerIndex, &layout));

    CHECK_EQ(layout.size.width, expectedLayout.size.width);
    CHECK_EQ(layout.size.height, expectedLayout.size.height);
    CHECK_EQ(layout.size.depth, expectedLayout.size.depth);
    CHECK_EQ(layout.sizeInBytes, expectedLayout.sizeInBytes);
    CHECK_EQ(layout.strideY, expectedLayout.strideY);
    CHECK_EQ(layout.strideZ, expectedLayout.strideZ);
}

GPU_TEST_CASE("texture-layout-1d-nomip", ALL)
{

    TextureDesc desc;
    desc.type = TextureType::Texture1D;
    desc.size = {256, 1, 1};
    desc.format = Format::R8G8B8A8_UINT;
    desc.mipLevelCount = 1;
    desc.arrayLength = 1;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {256, 1, 1, 1024, 1024, 1024});
}

// Checks layout adheres to the known 256B alignment of D3D12 and WGPU
GPU_TEST_CASE("texture-layout-1d-nomip-alignment", D3D12 | WGPU)
{

    TextureDesc desc;
    desc.type = TextureType::Texture1D;
    desc.size = {4, 1, 1};
    desc.format = Format::R8G8B8A8_UINT;
    desc.mipLevelCount = 1;
    desc.arrayLength = 1;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {4, 1, 1, 256, 256, 256});
}

GPU_TEST_CASE("texture-layout-1d-mips", ALL)
{

    TextureDesc desc;
    desc.type = TextureType::Texture1D;
    desc.size = {256, 1, 1};
    desc.format = Format::R8G8B8A8_UINT;
    desc.mipLevelCount = 0;
    desc.arrayLength = 1;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {256, 1, 1, 1024, 1024, 1024});
    testTextureLayout(device, texture, 0, 1, {128, 1, 1, 512, 512, 512});
}

GPU_TEST_CASE("texture-layout-1darray-nomip", ALL)
{

    TextureDesc desc;
    desc.type = TextureType::Texture1D;
    desc.size = {256, 1, 1};
    desc.format = Format::R8G8B8A8_UINT;
    desc.mipLevelCount = 1;
    desc.arrayLength = 4;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {256, 1, 1, 1024, 1024, 1024});
    testTextureLayout(device, texture, 3, 0, {256, 1, 1, 1024, 1024, 1024});
}

GPU_TEST_CASE("texture-layout-1darray-mips", ALL)
{

    TextureDesc desc;
    desc.type = TextureType::Texture1D;
    desc.size = {256, 1, 1};
    desc.format = Format::R8G8B8A8_UINT;
    desc.mipLevelCount = 0;
    desc.arrayLength = 4;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {256, 1, 1, 1024, 1024, 1024});
    testTextureLayout(device, texture, 0, 1, {128, 1, 1, 512, 512, 512});
    testTextureLayout(device, texture, 3, 0, {256, 1, 1, 1024, 1024, 1024});
    testTextureLayout(device, texture, 3, 1, {128, 1, 1, 512, 512, 512});
}

GPU_TEST_CASE("texture-layout-2d-nomip", ALL)
{

    TextureDesc desc;
    desc.type = TextureType::Texture2D;
    desc.size = {256, 32, 1};
    desc.format = Format::R8G8B8A8_UINT;
    desc.mipLevelCount = 1;
    desc.arrayLength = 1;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {256, 32, 1, 1024, 1024 * 32, 1024 * 32});
}

GPU_TEST_CASE("texture-layout-2d-mip", ALL)
{

    TextureDesc desc;
    desc.type = TextureType::Texture2D;
    desc.size = {256, 32, 1};
    desc.format = Format::R8G8B8A8_UINT;
    desc.mipLevelCount = 0;
    desc.arrayLength = 1;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {256, 32, 1, 1024, 1024 * 32, 1024 * 32});
    testTextureLayout(device, texture, 0, 1, {128, 16, 1, 512, 512 * 16, 512 * 16});
}

GPU_TEST_CASE("texture-layout-2d-array-nomip", ALL)
{

    TextureDesc desc;
    desc.type = TextureType::Texture2D;
    desc.size = {256, 32, 1};
    desc.format = Format::R8G8B8A8_UINT;
    desc.mipLevelCount = 1;
    desc.arrayLength = 4;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {256, 32, 1, 1024, 1024 * 32, 1024 * 32});
    testTextureLayout(device, texture, 3, 0, {256, 32, 1, 1024, 1024 * 32, 1024 * 32});
}

GPU_TEST_CASE("texture-layout-2d-array-mips", ALL)
{

    TextureDesc desc;
    desc.type = TextureType::Texture2D;
    desc.size = {256, 32, 1};
    desc.format = Format::R8G8B8A8_UINT;
    desc.mipLevelCount = 0;
    desc.arrayLength = 4;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {256, 32, 1, 1024, 1024 * 32, 1024 * 32});
    testTextureLayout(device, texture, 0, 1, {128, 16, 1, 512, 512 * 16, 512 * 16});
    testTextureLayout(device, texture, 3, 0, {256, 32, 1, 1024, 1024 * 32, 1024 * 32});
    testTextureLayout(device, texture, 3, 1, {128, 16, 1, 512, 512 * 16, 512 * 16});
}

GPU_TEST_CASE("texture-layout-3d-nomip", ALL)
{

    TextureDesc desc;
    desc.type = TextureType::Texture3D;
    desc.size = {256, 32, 16};
    desc.format = Format::R8G8B8A8_UINT;
    desc.mipLevelCount = 1;
    desc.arrayLength = 1;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {256, 32, 16, 1024, 1024 * 32, 1024 * 32 * 16});
}

GPU_TEST_CASE("texture-layout-3d-mip", ALL)
{

    TextureDesc desc;
    desc.type = TextureType::Texture3D;
    desc.size = {256, 32, 16};
    desc.format = Format::R8G8B8A8_UINT;
    desc.mipLevelCount = 0;
    desc.arrayLength = 1;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {256, 32, 16, 1024, 1024 * 32, 1024 * 32 * 16});
    testTextureLayout(device, texture, 0, 1, {128, 16, 8, 512, 512 * 16, 512 * 16 * 8});
}

GPU_TEST_CASE("texture-layout-cube-nomip", ALL)
{

    TextureDesc desc;
    desc.type = TextureType::TextureCube;
    desc.size = {256, 32, 1};
    desc.format = Format::R8G8B8A8_UINT;
    desc.mipLevelCount = 1;
    desc.arrayLength = 1;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {256, 32, 1, 1024, 1024 * 32, 1024 * 32});
}

GPU_TEST_CASE("texture-layout-cube-mip", ALL)
{

    TextureDesc desc;
    desc.type = TextureType::TextureCube;
    desc.size = {256, 32, 1};
    desc.format = Format::R8G8B8A8_UINT;
    desc.mipLevelCount = 0;
    desc.arrayLength = 1;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {256, 32, 1, 1024, 1024 * 32, 1024 * 32});
    testTextureLayout(device, texture, 0, 1, {128, 16, 1, 512, 512 * 16, 512 * 16});
}

GPU_TEST_CASE("texture-layout-cube-array-nomip", ALL)
{

    TextureDesc desc;
    desc.type = TextureType::TextureCube;
    desc.size = {256, 32, 1};
    desc.format = Format::R8G8B8A8_UINT;
    desc.mipLevelCount = 1;
    desc.arrayLength = 4;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {256, 32, 1, 1024, 1024 * 32, 1024 * 32});
    testTextureLayout(device, texture, 3 * 6, 0, {256, 32, 1, 1024, 1024 * 32, 1024 * 32});
}

GPU_TEST_CASE("texture-layout-cube-array-mips", ALL)
{

    TextureDesc desc;
    desc.type = TextureType::TextureCube;
    desc.size = {256, 32, 1};
    desc.format = Format::R8G8B8A8_UINT;
    desc.mipLevelCount = 0;
    desc.arrayLength = 4;

    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(desc, nullptr, texture.writeRef()));

    testTextureLayout(device, texture, 0, 0, {256, 32, 1, 1024, 1024 * 32, 1024 * 32});
    testTextureLayout(device, texture, 0, 1, {128, 16, 1, 512, 512 * 16, 512 * 16});
    testTextureLayout(device, texture, 3 * 6, 0, {256, 32, 1, 1024, 1024 * 32, 1024 * 32});
    testTextureLayout(device, texture, 3 * 6, 1, {128, 16, 1, 512, 512 * 16, 512 * 16});
}
