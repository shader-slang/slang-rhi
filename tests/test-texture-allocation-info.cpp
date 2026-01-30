#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("texture-allocation-info-2d", D3D12 | Vulkan | Metal | CUDA)
{
    TextureDesc desc;
    desc.type = TextureType::Texture2D;
    desc.size = {256, 256, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = 1;
    desc.arrayLength = 1;
    desc.usage = TextureUsage::ShaderResource;

    Size size = 0;
    Size alignment = 0;
    REQUIRE_CALL(device->getTextureAllocationInfo(desc, &size, &alignment));

    // Size should be at least width * height * bytes per pixel
    CHECK_GE(size, 256 * 256 * 4);
    // Alignment should be non-zero and a power of 2
    CHECK_GT(alignment, 0);
    CHECK_EQ(alignment & (alignment - 1), 0);
}

GPU_TEST_CASE("texture-allocation-info-2d-mips", D3D12 | Vulkan | Metal | CUDA)
{
    TextureDesc desc;
    desc.type = TextureType::Texture2D;
    desc.size = {256, 256, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = kAllMips;
    desc.arrayLength = 1;
    desc.usage = TextureUsage::ShaderResource;

    Size size = 0;
    Size alignment = 0;
    REQUIRE_CALL(device->getTextureAllocationInfo(desc, &size, &alignment));

    // Size should be at least width * height * bytes per pixel (for mip 0 alone)
    CHECK_GE(size, 256 * 256 * 4);
    // Alignment should be non-zero and a power of 2
    CHECK_GT(alignment, 0);
    CHECK_EQ(alignment & (alignment - 1), 0);
}

GPU_TEST_CASE("texture-allocation-info-3d", D3D12 | Vulkan | Metal | CUDA)
{
    TextureDesc desc;
    desc.type = TextureType::Texture3D;
    desc.size = {64, 64, 64};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = 1;
    desc.arrayLength = 1;
    desc.usage = TextureUsage::ShaderResource;

    Size size = 0;
    Size alignment = 0;
    REQUIRE_CALL(device->getTextureAllocationInfo(desc, &size, &alignment));

    // Size should be at least width * height * depth * bytes per pixel
    CHECK_GE(size, 64 * 64 * 64 * 4);
    // Alignment should be non-zero and a power of 2
    CHECK_GT(alignment, 0);
    CHECK_EQ(alignment & (alignment - 1), 0);
}

GPU_TEST_CASE("texture-allocation-info-array", D3D12 | Vulkan | Metal | CUDA)
{
    TextureDesc desc;
    desc.type = TextureType::Texture2DArray;
    desc.size = {128, 128, 1};
    desc.format = Format::RGBA8Uint;
    desc.mipCount = 1;
    desc.arrayLength = 4;
    desc.usage = TextureUsage::ShaderResource;

    Size size = 0;
    Size alignment = 0;
    REQUIRE_CALL(device->getTextureAllocationInfo(desc, &size, &alignment));

    // Size should be at least width * height * layers * bytes per pixel
    CHECK_GE(size, 128 * 128 * 4 * 4);
    // Alignment should be non-zero and a power of 2
    CHECK_GT(alignment, 0);
    CHECK_EQ(alignment & (alignment - 1), 0);
}
