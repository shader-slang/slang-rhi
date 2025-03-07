#include "testing.h"

#include "core/common.h"

#include <memory>

using namespace rhi;
using namespace rhi::testing;

inline uint32_t calcMipLevelCount(const TextureDesc& desc)
{
    if (desc.mipLevelCount == kAllMipLevels)
    {
        uint32_t maxDim = desc.size.width;
        if (desc.size.height > maxDim)
            maxDim = desc.size.height;
        if (desc.size.depth > maxDim)
            maxDim = desc.size.depth;
        return (uint32_t)log2(maxDim) + 1;
    }
    return desc.mipLevelCount;
}

inline uint32_t calcLayerCount(const TextureDesc& desc)
{
    switch (desc.type)
    {
    case TextureType::Texture1D:
    case TextureType::Texture2D:
    case TextureType::Texture2DMS:
    case TextureType::Texture3D:
        return 1;
    case TextureType::Texture1DArray:
    case TextureType::Texture2DArray:
    case TextureType::Texture2DMSArray:
        return desc.arrayLength;
    case TextureType::TextureCube:
        return 6;
    case TextureType::TextureCubeArray:
        return desc.arrayLength * 6;
    }
    return 0;
}

inline Extents calcMipSize(const Extents& size, uint32_t mipLevel)
{
    Extents mipSize = size;
    mipSize.width = max(1, mipSize.width >> mipLevel);
    mipSize.height = max(1, mipSize.height >> mipLevel);
    mipSize.depth = max(1, mipSize.depth >> mipLevel);
    return mipSize;
}

enum class TextureDimension
{
    Texture1D,
    Texture2D,
    Texture3D,
    TextureCube,
};

inline TextureDimension getTextureDimension(TextureType type)
{
    switch (type)
    {
    case TextureType::Texture1D:
    case TextureType::Texture1DArray:
        return TextureDimension::Texture1D;
    case TextureType::Texture2D:
    case TextureType::Texture2DArray:
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
        return TextureDimension::Texture2D;
    case TextureType::Texture3D:
        return TextureDimension::Texture3D;
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
        return TextureDimension::TextureCube;
    }
}

struct TestTextureData
{
    uint32_t mipLevelCount;
    uint32_t layerCount;
    uint32_t subresourceCount;
    size_t texelSize;

    struct Subresource
    {
        uint32_t mipLevel;
        uint32_t layer;
        Extents extents;
        std::unique_ptr<uint8_t[]> data;
        size_t dataSize;
        SubresourceData subresourceData;
    };

    std::vector<Subresource> subresources;
    std::vector<SubresourceData> subresourceData;

    struct rgba32
    {
        uint32_t r, g, b, a;
        bool operator==(const rgba32& rhs) const { return r == rhs.r && g == rhs.g && b == rhs.b && a == rhs.a; }
    };


    void initialize(const TextureDesc& desc)
    {
        mipLevelCount = calcMipLevelCount(desc);
        layerCount = calcLayerCount(desc);
        subresourceCount = mipLevelCount * layerCount;
        texelSize = getFormatInfo(desc.format).blockSizeInBytes;

        REQUIRE(desc.format == Format::R32G32B32A32_UINT);

        for (uint32_t layer = 0; layer < layerCount; ++layer)
        {
            for (uint32_t mipLevel = 0; mipLevel < mipLevelCount; ++mipLevel)
            {
                Subresource sr;
                sr.layer = layer;
                sr.mipLevel = mipLevel;
                sr.extents = calcMipSize(desc.size, mipLevel);
                sr.dataSize = sr.extents.width * sr.extents.height * sr.extents.depth * texelSize;
                sr.data = std::unique_ptr<uint8_t[]>(new uint8_t[sr.dataSize]);
                sr.subresourceData.data = sr.data.get();
                sr.subresourceData.strideY = sr.extents.width * texelSize;
                sr.subresourceData.strideZ = sr.extents.height * sr.subresourceData.strideY;
                fillSubresourceData(sr);

                subresourceData.push_back(sr.subresourceData);
                subresources.push_back(std::move(sr));
            }
        }
    }

    void validate(uint32_t layer, uint32_t mipLevel, const void* data, size_t size, size_t rowPitch, size_t pixelSize)
    {
        const Subresource& sr = subresources[layer * mipLevelCount + mipLevel];
        CHECK(size >= sr.dataSize);
        CHECK(rowPitch >= sr.subresourceData.strideY);
        CHECK(pixelSize == texelSize);
        for (uint32_t z = 0; z < sr.extents.depth; ++z)
        {
            for (uint32_t y = 0; y < sr.extents.height; ++y)
            {
                const uint8_t* expectedData = (const uint8_t*)sr.subresourceData.data + z * sr.subresourceData.strideZ +
                                              y * sr.subresourceData.strideY;
                const rgba32* expectedTexel = (const rgba32*)expectedData;
                const uint8_t* actualData = (const uint8_t*)data + (z * sr.extents.height + y) * rowPitch;
                const rgba32* actualTexel = (const rgba32*)actualData;
                for (uint32_t x = 0; x < sr.extents.width; ++x)
                {
                    CHECK(*actualTexel == *expectedTexel);
                    expectedTexel++;
                    actualTexel++;
                }
            }
        }
    }

    void fillSubresourceData(Subresource& sr)
    {
        for (uint32_t z = 0; z < sr.extents.depth; ++z)
        {
            for (uint32_t y = 0; y < sr.extents.height; ++y)
            {
                uint8_t* data =
                    (uint8_t*)sr.subresourceData.data + z * sr.subresourceData.strideZ + y * sr.subresourceData.strideY;
                rgba32* texel = (rgba32*)data;
                for (uint32_t x = 0; x < sr.extents.width; ++x)
                {
                    texel->r = x;
                    texel->g = y;
                    texel->b = z;
                    texel->a = (sr.mipLevel << 16) | sr.layer;
                    texel++;
                }
            }
        }
    }
};


struct CreateTextureTestSpec
{
    TextureType type;
    Format format;
    Extents size;
    uint32_t mipLevelCount;
    uint32_t arrayLength;
};

// clang-format off
static const CreateTextureTestSpec kCreateTextureTestSpecs[] = {
//    type                              format                      size                mipLevelCount   arrayLength
    { TextureType::Texture1D,           Format::R32G32B32A32_UINT,  { 128, 1, 1 },      1,              1,              },
    { TextureType::Texture1D,           Format::R32G32B32A32_UINT,  { 128, 1, 1 },      kAllMipLevels,  1,              },
    { TextureType::Texture1DArray,      Format::R32G32B32A32_UINT,  { 128, 1, 1 },      1,              1,              },
    { TextureType::Texture1DArray,      Format::R32G32B32A32_UINT,  { 128, 1, 1 },      kAllMipLevels,  1,              },
    { TextureType::Texture1DArray,      Format::R32G32B32A32_UINT,  { 128, 1, 1 },      1,              4,              },
    { TextureType::Texture1DArray,      Format::R32G32B32A32_UINT,  { 128, 1, 1 },      kAllMipLevels,  4,              },
    { TextureType::Texture2D,           Format::R32G32B32A32_UINT,  { 128, 64, 1 },     1,              1,              },
    { TextureType::Texture2D,           Format::R32G32B32A32_UINT,  { 128, 64, 1 },     kAllMipLevels,  1,              },
    { TextureType::Texture2DArray,      Format::R32G32B32A32_UINT,  { 128, 64, 1 },     1,              1,              },
    { TextureType::Texture2DArray,      Format::R32G32B32A32_UINT,  { 128, 64, 1 },     kAllMipLevels,  1,              },
    { TextureType::Texture2DArray,      Format::R32G32B32A32_UINT,  { 128, 64, 1 },     1,              4,              },
    { TextureType::Texture2DArray,      Format::R32G32B32A32_UINT,  { 128, 64, 1 },     kAllMipLevels,  4,              },
    { TextureType::Texture2DMS,         Format::R32G32B32A32_UINT,  { 128, 64, 1 },     1,              1               },
    { TextureType::Texture2DMSArray,    Format::R32G32B32A32_UINT,  { 128, 64, 1 },     1,              1               },
    { TextureType::Texture2DMSArray,    Format::R32G32B32A32_UINT,  { 128, 64, 1 },     1,              4               },
    { TextureType::Texture3D,           Format::R32G32B32A32_UINT,  { 128, 64, 32 },    1,              1,              },
    { TextureType::Texture3D,           Format::R32G32B32A32_UINT,  { 128, 64, 32 },    kAllMipLevels,  1,              },
    { TextureType::TextureCube,         Format::R32G32B32A32_UINT,  { 128, 128, 1 },    1,              1,              },
    { TextureType::TextureCube,         Format::R32G32B32A32_UINT,  { 128, 128, 1 },    kAllMipLevels,  1,              },
    { TextureType::TextureCubeArray,    Format::R32G32B32A32_UINT,  { 128, 128, 1 },    1,              1,              },
    { TextureType::TextureCubeArray,    Format::R32G32B32A32_UINT,  { 128, 128, 1 },    kAllMipLevels,  1,              },
    { TextureType::TextureCubeArray,    Format::R32G32B32A32_UINT,  { 128, 128, 1 },    1,              4,              },
    { TextureType::TextureCubeArray,    Format::R32G32B32A32_UINT,  { 128, 128, 1 },    kAllMipLevels,  4,              },
};
// clang-format on

GPU_TEST_CASE("texture-create", ALL & ~CUDA)
{
    for (const CreateTextureTestSpec& spec : kCreateTextureTestSpecs)
    {
        TextureDesc desc = {};
        desc.type = spec.type;
        desc.size = spec.size;
        desc.mipLevelCount = spec.mipLevelCount;
        desc.arrayLength = spec.arrayLength;
        desc.format = spec.format;
        desc.usage = TextureUsage::ShaderResource | TextureUsage::CopySource;

        TestTextureData testData;
        testData.initialize(desc);

        CAPTURE(desc.type);
        CAPTURE(desc.size.width);
        CAPTURE(desc.size.height);
        CAPTURE(desc.size.depth);
        CAPTURE(desc.mipLevelCount);
        CAPTURE(desc.arrayLength);
        CAPTURE(desc.format);

        ComPtr<ITexture> texture;
        Result result = device->createTexture(desc, testData.subresourceData.data(), texture.writeRef());

        bool expectFailure = false;
        // WGPU does not support mip levels for 1D textures.
        if (device->getDeviceType() == DeviceType::WGPU && desc.type == TextureType::Texture1D &&
            desc.mipLevelCount != 1)
            expectFailure = true;
        // WGPU does not support 1D texture arrays.
        if (device->getDeviceType() == DeviceType::WGPU && desc.type == TextureType::Texture1DArray)
            expectFailure = true;
        // CUDA does not support multisample textures.
        if (device->getDeviceType() == DeviceType::CUDA &&
            (desc.type == TextureType::Texture2DMS || desc.type == TextureType::Texture2DMSArray))
            expectFailure = true;

        if (expectFailure)
        {
            CHECK(!SLANG_SUCCEEDED(result));
            continue;
        }
        REQUIRE_CALL(result);

        uint32_t expectedMipLevelCount = calcMipLevelCount(desc);
        uint32_t expectedLayerCount = calcLayerCount(desc);

        CHECK(texture->getDesc().type == desc.type);
        CHECK(texture->getDesc().size.width == desc.size.width);
        CHECK(texture->getDesc().size.height == desc.size.height);
        CHECK(texture->getDesc().size.depth == desc.size.depth);
        CHECK(texture->getDesc().arrayLength == desc.arrayLength);
        CHECK(texture->getDesc().mipLevelCount == expectedMipLevelCount);
        CHECK(texture->getDesc().format == desc.format);
        CHECK(texture->getDesc().getLayerCount() == expectedLayerCount);

        // TODO: CPU readback not implemented
        if (device->getDeviceType() == DeviceType::CPU || device->getDeviceType() == DeviceType::D3D11)
        {
            continue;
        }

        for (uint32_t layer = 0; layer < expectedLayerCount; ++layer)
        {
            for (uint32_t mipLevel = 0; mipLevel < expectedMipLevelCount; ++mipLevel)
            {
                ComPtr<ISlangBlob> readbackData;
                size_t rowPitch;
                size_t pixelSize;
                REQUIRE_CALL(
                    device->readTexture(texture, layer, mipLevel, readbackData.writeRef(), &rowPitch, &pixelSize)
                );
                testData.validate(
                    layer,
                    mipLevel,
                    readbackData->getBufferPointer(),
                    readbackData->getBufferSize(),
                    rowPitch,
                    pixelSize
                );
            }
        }
    }
}
