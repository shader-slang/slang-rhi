#include "format-conversion.h"

#include <unordered_map>

namespace rhi {

static const FormatConversionFuncs sFuncs[] = {
    {Format::Undefined, nullptr, nullptr, nullptr, nullptr},

    {Format::R8Uint, packInt8<1>, unpackInt8<1>, nullptr, nullptr},
    {Format::R8Sint, packInt8<1>, unpackInt8<1>, nullptr, nullptr},
    {Format::R8Unorm, packInt8<1>, unpackInt8<1>, packUnorm8<1>, unpackUnorm8<1>},
    {Format::R8Snorm, packInt8<1>, unpackInt8<1>, packSnorm8<1>, unpackSnorm8<1>},

    {Format::RG8Uint, packInt8<2>, unpackInt8<2>, nullptr, nullptr},
    {Format::RG8Sint, packInt8<2>, unpackInt8<2>, nullptr, nullptr},
    {Format::RG8Unorm, packInt8<2>, unpackInt8<2>, packUnorm8<2>, unpackUnorm8<2>},
    {Format::RG8Snorm, packInt8<2>, unpackInt8<2>, packSnorm8<2>, unpackSnorm8<2>},

    {Format::RGBA8Uint, packInt8<4>, unpackInt8<4>, nullptr, nullptr},
    {Format::RGBA8Sint, packInt8<4>, unpackInt8<4>, nullptr, nullptr},
    {Format::RGBA8Unorm, packInt8<4>, unpackInt8<4>, packUnorm8<4>, unpackUnorm8<4>},
    {Format::RGBA8UnormSrgb, packInt8<4>, unpackInt8<4>, nullptr, nullptr},
    {Format::RGBA8Snorm, packInt8<4>, unpackInt8<4>, packSnorm8<4>, unpackSnorm8<4>},

    // TODO flip channels
    {Format::BGRA8Unorm, packInt8<4>, unpackInt8<4>, packUnormBGRA8, unpackUnormBGRA8},
    {Format::BGRA8UnormSrgb, packInt8<4>, unpackInt8<4>, nullptr, nullptr},
    // TODO should we discard last channel?
    {Format::BGRX8Unorm, packInt8<4>, unpackInt8<4>, packUnormBGRA8, unpackUnormBGRA8},
    {Format::BGRX8UnormSrgb, packInt8<4>, unpackInt8<4>, nullptr, nullptr},

    {Format::R16Uint, packInt16<1>, unpackInt16<1>, nullptr, nullptr},
    {Format::R16Sint, packInt16<1>, unpackInt16<1>, nullptr, nullptr},
    {Format::R16Unorm, packInt16<1>, unpackInt16<1>, packUnorm16<1>, unpackUnorm16<1>},
    {Format::R16Snorm, packInt16<1>, unpackInt16<1>, packSnorm16<1>, unpackSnorm16<1>},
    {Format::R16Float, packInt16<1>, unpackInt16<1>, packFloat16<1>, unpackFloat16<1>},

    {Format::RG16Uint, packInt16<2>, unpackInt16<2>, nullptr, nullptr},
    {Format::RG16Sint, packInt16<2>, unpackInt16<2>, nullptr, nullptr},
    {Format::RG16Unorm, packInt16<2>, unpackInt16<2>, packUnorm16<2>, unpackUnorm16<2>},
    {Format::RG16Snorm, packInt16<2>, unpackInt16<2>, packSnorm16<2>, unpackSnorm16<2>},
    {Format::RG16Float, packInt16<2>, unpackInt16<2>, packFloat16<2>, unpackFloat16<2>},

    {Format::RGBA16Uint, packInt16<4>, unpackInt16<4>, nullptr, nullptr},
    {Format::RGBA16Sint, packInt16<4>, unpackInt16<4>, nullptr, nullptr},
    {Format::RGBA16Unorm, packInt16<4>, unpackInt16<4>, packUnorm16<4>, unpackUnorm16<4>},
    {Format::RGBA16Snorm, packInt16<4>, unpackInt16<4>, packSnorm16<4>, unpackSnorm16<4>},
    {Format::RGBA16Float, packInt16<4>, unpackInt16<4>, packFloat16<4>, unpackFloat16<4>},

    {Format::R32Uint, packInt32<1>, unpackInt32<1>, nullptr, nullptr},
    {Format::R32Sint, packInt32<1>, unpackInt32<1>, nullptr, nullptr},
    {Format::R32Float, packInt32<1>, unpackInt32<1>, packFloat32<1>, unpackFloat32<1>},

    {Format::RG32Uint, packInt32<2>, unpackInt32<2>, nullptr, nullptr},
    {Format::RG32Sint, packInt32<2>, unpackInt32<2>, nullptr, nullptr},
    {Format::RG32Float, packInt32<2>, unpackInt32<2>, packFloat32<2>, unpackFloat32<2>},

    {Format::RGB32Uint, packInt32<3>, unpackInt32<3>, nullptr, nullptr},
    {Format::RGB32Sint, packInt32<3>, unpackInt32<3>, nullptr, nullptr},
    {Format::RGB32Float, packInt32<3>, unpackInt32<3>, packFloat32<3>, unpackFloat32<3>},

    {Format::RGBA32Uint, packInt32<4>, unpackInt32<4>, nullptr, nullptr},
    {Format::RGBA32Sint, packInt32<4>, unpackInt32<4>, nullptr, nullptr},
    {Format::RGBA32Float, packInt32<4>, unpackInt32<4>, packFloat32<4>, unpackFloat32<4>},

    {Format::R64Uint, nullptr, nullptr, nullptr, nullptr},
    {Format::R64Sint, nullptr, nullptr, nullptr, nullptr},

    {Format::BGRA4Unorm, packInt4444, unpackInt4444, packUnorm4444, unpackUnorm4444},
    {Format::B5G6R5Unorm, packInt565, unpackInt565, packUnorm565, unpackUnorm565},
    {Format::BGR5A1Unorm, packInt5551, unpackInt5551, packUnorm5551, unpackUnorm5551},

    {Format::RGB9E5Ufloat, nullptr, nullptr, nullptr, nullptr},
    {Format::RGB10A2Uint, nullptr, nullptr, nullptr, nullptr},
    {Format::RGB10A2Unorm, nullptr, nullptr, nullptr, nullptr},
    {Format::R11G11B10Float, nullptr, nullptr, nullptr, nullptr},

    {Format::D32Float, packInt32<1>, unpackInt32<1>, packFloat32<1>, unpackFloat32<1>},
    {Format::D16Unorm, packInt16<1>, unpackInt16<1>, packUnorm16<1>, unpackUnorm16<1>},
    {Format::D32FloatS8Uint, nullptr, nullptr, nullptr, nullptr},
};

FormatConversionFuncs getFormatConversionFuncs(Format format)
{
    SLANG_RHI_ASSERT(size_t(format) < size_t(Format::_Count));
    SLANG_RHI_ASSERT(sFuncs[size_t(format)].format == format);
    return sFuncs[size_t(format)];
}

} // namespace rhi
