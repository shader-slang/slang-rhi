#include "resource-desc-utils.h"

namespace rhi {

BufferDesc fixupBufferDesc(const BufferDesc& desc)
{
    BufferDesc result = desc;
    result.allowedStates.add(result.defaultState);
    return result;
}

TextureDesc fixupTextureDesc(const TextureDesc& desc)
{
    TextureDesc rs = desc;
    if (desc.numMipLevels == 0)
        rs.numMipLevels = calcNumMipLevels(desc.type, desc.size);
    rs.allowedStates.add(rs.defaultState);
    return rs;
}

Format srgbToLinearFormat(Format format)
{
    switch (format)
    {
    case Format::BC1_UNORM_SRGB:
        return Format::BC1_UNORM;
    case Format::BC2_UNORM_SRGB:
        return Format::BC2_UNORM;
    case Format::BC3_UNORM_SRGB:
        return Format::BC3_UNORM;
    case Format::BC7_UNORM_SRGB:
        return Format::BC7_UNORM;
    case Format::B8G8R8A8_UNORM_SRGB:
        return Format::B8G8R8A8_UNORM;
    case Format::B8G8R8X8_UNORM_SRGB:
        return Format::B8G8R8X8_UNORM;
    case Format::R8G8B8A8_UNORM_SRGB:
        return Format::R8G8B8A8_UNORM;
    default:
        return format;
    }
}

} // namespace rhi
