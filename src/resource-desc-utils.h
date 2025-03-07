#pragma once

#include <slang-rhi.h>

#include "core/common.h"

namespace rhi {

inline uint32_t calcMipSize(uint32_t size, uint32_t level)
{
    size = size >> level;
    return size > 0 ? size : 1;
}

inline Extents calcMipSize(Extents size, uint32_t mipLevel)
{
    Extents rs;
    rs.width = calcMipSize(size.width, mipLevel);
    rs.height = calcMipSize(size.height, mipLevel);
    rs.depth = calcMipSize(size.depth, mipLevel);
    return rs;
}

/// Given the type works out the maximum dimension size
inline uint32_t calcMaxDimension(Extents size, TextureType type)
{
    switch (type)
    {
    case TextureType::Texture1D:
    case TextureType::Texture1DArray:
        return size.width;
    case TextureType::Texture2D:
    case TextureType::Texture2DArray:
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
        return max(size.width, size.height);
    case TextureType::Texture3D:
        return max({size.width, size.height, size.depth});
    default:
        return 0;
    }
}

/// Given the type, calculates the number of mip maps. 0 on error
inline uint32_t calcMipLevelCount(TextureType type, Extents size)
{
    uint32_t maxDimensionSize = calcMaxDimension(size, type);
    return (maxDimensionSize > 0) ? (math::log2Floor(maxDimensionSize) + 1) : 0;
}

inline uint32_t calcMipLevelCount(const TextureDesc& desc)
{
    return calcMipLevelCount(desc.type, desc.size);
}

inline uint32_t calcLayerCount(TextureType type, uint32_t arrayLength)
{
    switch (type)
    {
    case TextureType::Texture1D:
    case TextureType::Texture2D:
    case TextureType::Texture2DMS:
    case TextureType::Texture3D:
        return 1;
    case TextureType::Texture1DArray:
    case TextureType::Texture2DArray:
    case TextureType::Texture2DMSArray:
        return arrayLength;
    case TextureType::TextureCube:
        return 6;
    case TextureType::TextureCubeArray:
        return arrayLength * 6;
    }
    return 0;
}

inline uint32_t calcLayerCount(const TextureDesc& desc)
{
    return calcLayerCount(desc.type, desc.arrayLength);
}


BufferDesc fixupBufferDesc(const BufferDesc& desc);
TextureDesc fixupTextureDesc(const TextureDesc& desc);

Format srgbToLinearFormat(Format format);

} // namespace rhi
