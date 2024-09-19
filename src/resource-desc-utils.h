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


BufferDesc fixupBufferDesc(const BufferDesc& desc);
TextureDesc fixupTextureDesc(const TextureDesc& desc);

Format srgbToLinearFormat(Format format);

} // namespace rhi
