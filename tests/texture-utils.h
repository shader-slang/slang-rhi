#pragma once

#include "testing.h"

#include <vector>

namespace rhi::testing {
struct Strides
{
    Size x;
    Size y;
    Size z;
};

struct ValidationTextureFormatBase : RefObject
{
    virtual void validateBlocksEqual(const void* actual, const void* expected) = 0;

    virtual void initializeTexel(
        void* texel,
        uint32_t x,
        uint32_t y,
        uint32_t z,
        uint32_t mip,
        uint32_t arrayLayer
    ) = 0;
};

template<typename T>
struct ValidationTextureFormat : ValidationTextureFormatBase
{
    uint32_t componentCount;

    ValidationTextureFormat(uint32_t componentCount)
        : componentCount(componentCount) {};

    virtual void validateBlocksEqual(const void* actual, const void* expected) override
    {
        auto a = (const T*)actual;
        auto e = (const T*)expected;

        for (uint32_t i = 0; i < componentCount; ++i)
        {
            CHECK_EQ(a[i], e[i]);
        }
    }

    virtual void initializeTexel(
        void* texel,
        uint32_t x,
        uint32_t y,
        uint32_t z,
        uint32_t mip,
        uint32_t arrayLayer
    ) override
    {
        auto temp = (T*)texel;

        switch (componentCount)
        {
        case 1:
            temp[0] = T(x + y + z + mip + arrayLayer);
            break;
        case 2:
            temp[0] = T(x + z + arrayLayer);
            temp[1] = T(y + mip);
            break;
        case 3:
            temp[0] = T(x + mip);
            temp[1] = T(y + arrayLayer);
            temp[2] = T(z);
            break;
        case 4:
            temp[0] = T(x + arrayLayer);
            temp[1] = (T)y;
            temp[2] = (T)z;
            temp[3] = (T)mip;
            break;
        default:
            MESSAGE("component count should be no greater than 4");
            REQUIRE(false);
        }
    }
};

template<typename T>
struct PackedValidationTextureFormat : ValidationTextureFormatBase
{
    int rBits;
    int gBits;
    int bBits;
    int aBits;

    PackedValidationTextureFormat(int rBits, int gBits, int bBits, int aBits)
        : rBits(rBits)
        , gBits(gBits)
        , bBits(bBits)
        , aBits(aBits) {};

    virtual void validateBlocksEqual(const void* actual, const void* expected) override
    {
        T a[4];
        T e[4];
        unpackTexel(*(const T*)actual, a);
        unpackTexel(*(const T*)expected, e);

        for (uint32_t i = 0; i < 4; ++i)
        {
            CHECK_EQ(a[i], e[i]);
        }
    }

    virtual void initializeTexel(
        void* texel,
        uint32_t x,
        uint32_t y,
        uint32_t z,
        uint32_t mip,
        uint32_t arrayLayer
    ) override
    {
        T temp = 0;

        // The only formats which currently use this have either 3 or 4 channels. TODO: BC formats?
        if (aBits == 0)
        {
            temp |= z;
            temp <<= gBits;
            temp |= (y + arrayLayer);
            temp <<= rBits;
            temp |= (x + mip);
        }
        else
        {
            temp |= mip;
            temp <<= bBits;
            temp |= z;
            temp <<= gBits;
            temp |= y;
            temp <<= rBits;
            temp |= (x + arrayLayer);
        }

        *(T*)texel = temp;
    }

    void unpackTexel(T texel, T* outComponents)
    {
        outComponents[0] = texel & ((1 << rBits) - 1);
        texel >>= rBits;

        outComponents[1] = texel & ((1 << gBits) - 1);
        texel >>= gBits;

        outComponents[2] = texel & ((1 << bBits) - 1);
        texel >>= bBits;

        outComponents[3] = texel & ((1 << aBits) - 1);
        texel >>= aBits;
    }
};

// Struct containing texture data and information for a specific subresource.
struct ValidationTextureData : RefObject
{
    const void* textureData;
    Extent3D extent;
    Strides pitches;

    void* getBlockAt(uint32_t x, uint32_t y, uint32_t z)
    {
        SLANG_RHI_ASSERT(x < extent.width);
        SLANG_RHI_ASSERT(y < extent.height);
        SLANG_RHI_ASSERT(z < extent.depth);

        char* layerData = (char*)textureData + z * pitches.z;
        char* rowData = layerData + y * pitches.y;
        return rowData + x * pitches.x;
    }
};

// Struct containing relevant information for a texture, including a list of its subresources
// and all relevant information for each subresource.
struct TextureInfo : RefObject
{
    Format format;
    TextureType textureType;

    Extent3D extent;
    uint32_t mipCount;
    uint32_t arrayLength;

    std::vector<RefPtr<ValidationTextureData>> subresourceObjects;
    std::vector<SubresourceData> subresourceDatas;

    ~TextureInfo();
};

inline TextureType toArrayType(TextureType type)
{
    switch (type)
    {
    case TextureType::Texture1D:
        return TextureType::Texture1DArray;
    case TextureType::Texture2D:
        return TextureType::Texture2DArray;
    case TextureType::Texture2DMS:
        return TextureType::Texture2DMSArray;
    case TextureType::TextureCube:
        return TextureType::TextureCubeArray;
    default:
        return type;
    }
}

Size getTexelSize(Format format);
RefPtr<ValidationTextureFormatBase> getValidationTextureFormat(Format format);
void generateTextureData(RefPtr<TextureInfo> texture, ValidationTextureFormatBase* validationFormat);

std::vector<uint8_t> removePadding(ISlangBlob* pixels, uint32_t width, uint32_t height, Size rowPitch, Size pixelSize);
Result writeImage(const char* filename, ISlangBlob* pixels, uint32_t width, uint32_t height);
Result writeImage(
    const char* filename,
    ISlangBlob* pixels,
    uint32_t width,
    uint32_t height,
    uint32_t rowPitch,
    uint32_t pixelSize
);

} // namespace rhi::testing
