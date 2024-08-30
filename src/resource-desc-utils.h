#pragma once

#include <slang-rhi.h>

#include "core/common.h"

namespace rhi {

inline int calcMipSize(int size, int level)
{
    size = size >> level;
    return size > 0 ? size : 1;
}

inline Extents calcMipSize(Extents size, int mipLevel)
{
    Extents rs;
    rs.width = calcMipSize(size.width, mipLevel);
    rs.height = calcMipSize(size.height, mipLevel);
    rs.depth = calcMipSize(size.depth, mipLevel);
    return rs;
}

/// Calculate the effective array size - in essence the amount if mip map sets needed.
/// In practice takes into account if the arraySize is 0 (it's not an array, but it will still have
/// at least one mip set) and if the type is a cubemap (multiplies the amount of mip sets by 6)
inline int calcEffectiveArraySize(const TextureDesc& desc)
{
    const int arrSize = (desc.arraySize > 0) ? desc.arraySize : 1;

    switch (desc.type)
    {
    case TextureType::Texture1D: // fallthru
    case TextureType::Texture2D:
    {
        return arrSize;
    }
    case TextureType::TextureCube:
        return arrSize * 6;
    case TextureType::Texture3D:
        return 1;
    default:
        return 0;
    }
}

/// Given the type works out the maximum dimension size
inline int calcMaxDimension(Extents size, TextureType type)
{
    switch (type)
    {
    case TextureType::Texture1D:
        return size.width;
    case TextureType::Texture3D:
        return std::max(std::max(size.width, size.height), size.depth);
    case TextureType::TextureCube: // fallthru
    case TextureType::Texture2D:
    {
        return std::max(size.width, size.height);
    }
    default:
        return 0;
    }
}

/// Given the type, calculates the number of mip maps. 0 on error
inline int calcNumMipLevels(TextureType type, Extents size)
{
    const int maxDimensionSize = calcMaxDimension(size, type);
    return (maxDimensionSize > 0) ? (math::log2Floor(maxDimensionSize) + 1) : 0;
}

/// Calculate the total number of sub resources. 0 on error.
inline int calcNumSubResources(const TextureDesc& desc)
{
    const int numMipMaps = (desc.numMipLevels > 0) ? desc.numMipLevels : calcNumMipLevels(desc.type, desc.size);
    const int arrSize = (desc.arraySize > 0) ? desc.arraySize : 1;

    switch (desc.type)
    {
    case TextureType::Texture1D:
    case TextureType::Texture2D:
    case TextureType::Texture3D:
    {
        return numMipMaps * arrSize;
    }
    case TextureType::TextureCube:
    {
        // There are 6 faces to a cubemap
        return numMipMaps * arrSize * 6;
    }
    default:
        return 0;
    }
}

BufferDesc fixupBufferDesc(const BufferDesc& desc);
TextureDesc fixupTextureDesc(const TextureDesc& desc);

Format srgbToLinearFormat(Format format);

} // namespace rhi
