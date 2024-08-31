#pragma once

#include <slang-rhi.h>

#include "core/common.h"

namespace rhi {

inline int calcMipSize(int size, int level)
{
    size = size >> level;
    return size > 0 ? size : 1;
}

inline ITexture::Extents calcMipSize(ITexture::Extents size, int mipLevel)
{
    ITexture::Extents rs;
    rs.width = calcMipSize(size.width, mipLevel);
    rs.height = calcMipSize(size.height, mipLevel);
    rs.depth = calcMipSize(size.depth, mipLevel);
    return rs;
}

/// Calculate the effective array size - in essence the amount if mip map sets needed.
/// In practice takes into account if the arraySize is 0 (it's not an array, but it will still have
/// at least one mip set) and if the type is a cubemap (multiplies the amount of mip sets by 6)
inline int calcEffectiveArraySize(const ITexture::Desc& desc)
{
    const int arrSize = (desc.arraySize > 0) ? desc.arraySize : 1;

    switch (desc.type)
    {
    case IResource::Type::Texture1D: // fallthru
    case IResource::Type::Texture2D:
    {
        return arrSize;
    }
    case IResource::Type::TextureCube:
        return arrSize * 6;
    case IResource::Type::Texture3D:
        return 1;
    default:
        return 0;
    }
}

/// Given the type works out the maximum dimension size
inline int calcMaxDimension(ITexture::Extents size, IResource::Type type)
{
    switch (type)
    {
    case IResource::Type::Texture1D:
        return size.width;
    case IResource::Type::Texture3D:
        return std::max(std::max(size.width, size.height), size.depth);
    case IResource::Type::TextureCube: // fallthru
    case IResource::Type::Texture2D:
    {
        return std::max(size.width, size.height);
    }
    default:
        return 0;
    }
}

/// Given the type, calculates the number of mip maps. 0 on error
inline int calcNumMipLevels(IResource::Type type, ITexture::Extents size)
{
    const int maxDimensionSize = calcMaxDimension(size, type);
    return (maxDimensionSize > 0) ? (math::log2Floor(maxDimensionSize) + 1) : 0;
}

/// Calculate the total number of sub resources. 0 on error.
inline int calcNumSubResources(const ITexture::Desc& desc)
{
    const int numMipMaps = (desc.numMipLevels > 0) ? desc.numMipLevels : calcNumMipLevels(desc.type, desc.size);
    const int arrSize = (desc.arraySize > 0) ? desc.arraySize : 1;

    switch (desc.type)
    {
    case IResource::Type::Texture1D:
    case IResource::Type::Texture2D:
    case IResource::Type::Texture3D:
    {
        return numMipMaps * arrSize;
    }
    case IResource::Type::TextureCube:
    {
        // There are 6 faces to a cubemap
        return numMipMaps * arrSize * 6;
    }
    default:
        return 0;
    }
}

IBuffer::Desc fixupBufferDesc(const IBuffer::Desc& desc);
ITexture::Desc fixupTextureDesc(const ITexture::Desc& desc);

Format srgbToLinearFormat(Format format);

} // namespace rhi
