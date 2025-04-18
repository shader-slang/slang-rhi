#pragma once

#include <slang-rhi.h>

#include "core/common.h"

namespace rhi {

inline uint32_t calcMipSize(uint32_t size, uint32_t level)
{
    size = size >> level;
    return size > 0 ? size : 1;
}

inline Extent3D calcMipSize(Extent3D size, uint32_t mip)
{
    Extent3D rs;
    rs.width = calcMipSize(size.width, mip);
    rs.height = calcMipSize(size.height, mip);
    rs.depth = calcMipSize(size.depth, mip);
    return rs;
}

/// Given the type works out the maximum dimension size
inline uint32_t calcMaxDimension(Extent3D size, TextureType type)
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
inline uint32_t calcMipCount(TextureType type, Extent3D size)
{
    uint32_t maxDimensionSize = calcMaxDimension(size, type);
    return (maxDimensionSize > 0) ? (math::log2Floor(maxDimensionSize) + 1) : 0;
}

inline uint32_t calcMipCount(const TextureDesc& desc)
{
    return calcMipCount(desc.type, desc.size);
}

BufferDesc fixupBufferDesc(const BufferDesc& desc);
TextureDesc fixupTextureDesc(const TextureDesc& desc);

Format srgbToLinearFormat(Format format);

} // namespace rhi
