#include "format-conversion.h"

#include <unordered_map>

namespace rhi {

inline void clampNoOp(uint32_t[4]) {}

static const FormatConversionFuncs sFuncs[] = {
    {Format::Undefined, nullptr, nullptr, nullptr, nullptr, nullptr},

    {Format::R8Uint, packUint8<1>, unpackUint8<1>, clampUint8<1>, nullptr, nullptr},
    {Format::R8Sint, packSint8<1>, unpackSint8<1>, clampSint8<1>, nullptr, nullptr},
    {Format::R8Unorm, nullptr, nullptr, nullptr, packUnorm8<1>, unpackUnorm8<1>},
    {Format::R8Snorm, nullptr, nullptr, nullptr, packSnorm8<1>, unpackSnorm8<1>},

    {Format::RG8Uint, packUint8<2>, unpackUint8<2>, clampUint8<2>, nullptr, nullptr},
    {Format::RG8Sint, packSint8<2>, unpackSint8<2>, clampSint8<2>, nullptr, nullptr},
    {Format::RG8Unorm, nullptr, nullptr, nullptr, packUnorm8<2>, unpackUnorm8<2>},
    {Format::RG8Snorm, nullptr, nullptr, nullptr, packSnorm8<2>, unpackSnorm8<2>},

    {Format::RGBA8Uint, packUint8<4>, unpackUint8<4>, clampUint8<4>, nullptr, nullptr},
    {Format::RGBA8Sint, packSint8<4>, unpackSint8<4>, clampSint8<4>, nullptr, nullptr},
    {Format::RGBA8Unorm, nullptr, nullptr, nullptr, packUnorm8<4>, unpackUnorm8<4>},
    {Format::RGBA8UnormSrgb, nullptr, nullptr, nullptr, packRGBA8UnormSrgb, unpackRGBA8UnormSrgb},
    {Format::RGBA8Snorm, nullptr, nullptr, nullptr, packSnorm8<4>, unpackSnorm8<4>},

    // TODO flip channels
    {Format::BGRA8Unorm, nullptr, nullptr, nullptr, packBGRA8Unorm, unpackBGRA8Unorm},
    {Format::BGRA8UnormSrgb, nullptr, nullptr, nullptr, packBGRA8UnormSrgb, unpackBGRA8UnormSrgb},
    // TODO should we discard last channel?
    {Format::BGRX8Unorm, nullptr, nullptr, nullptr, packBGRA8Unorm, unpackBGRA8Unorm},
    {Format::BGRX8UnormSrgb, nullptr, nullptr, nullptr, packBGRA8UnormSrgb, unpackBGRA8UnormSrgb},

    {Format::R16Uint, packUint16<1>, unpackUint16<1>, clampUint16<1>, nullptr, nullptr},
    {Format::R16Sint, packSint16<1>, unpackSint16<1>, clampSint16<1>, nullptr, nullptr},
    {Format::R16Unorm, nullptr, nullptr, nullptr, packUnorm16<1>, unpackUnorm16<1>},
    {Format::R16Snorm, nullptr, nullptr, nullptr, packSnorm16<1>, unpackSnorm16<1>},
    {Format::R16Float, nullptr, nullptr, nullptr, packFloat16<1>, unpackFloat16<1>},

    {Format::RG16Uint, packUint16<2>, unpackUint16<2>, clampUint16<2>, nullptr, nullptr},
    {Format::RG16Sint, packSint16<2>, unpackSint16<2>, clampSint16<2>, nullptr, nullptr},
    {Format::RG16Unorm, nullptr, nullptr, nullptr, packUnorm16<2>, unpackUnorm16<2>},
    {Format::RG16Snorm, nullptr, nullptr, nullptr, packSnorm16<2>, unpackSnorm16<2>},
    {Format::RG16Float, nullptr, nullptr, nullptr, packFloat16<2>, unpackFloat16<2>},

    {Format::RGBA16Uint, packUint16<4>, unpackUint16<4>, clampUint16<4>, nullptr, nullptr},
    {Format::RGBA16Sint, packSint16<4>, unpackSint16<4>, clampSint16<4>, nullptr, nullptr},
    {Format::RGBA16Unorm, nullptr, nullptr, nullptr, packUnorm16<4>, unpackUnorm16<4>},
    {Format::RGBA16Snorm, nullptr, nullptr, nullptr, packSnorm16<4>, unpackSnorm16<4>},
    {Format::RGBA16Float, nullptr, nullptr, nullptr, packFloat16<4>, unpackFloat16<4>},

    {Format::R32Uint, packInt32<1>, unpackInt32<1>, clampNoOp, nullptr, nullptr},
    {Format::R32Sint, packInt32<1>, unpackInt32<1>, clampNoOp, nullptr, nullptr},
    {Format::R32Float, nullptr, nullptr, nullptr, packFloat32<1>, unpackFloat32<1>},

    {Format::RG32Uint, packInt32<2>, unpackInt32<2>, clampNoOp, nullptr, nullptr},
    {Format::RG32Sint, packInt32<2>, unpackInt32<2>, clampNoOp, nullptr, nullptr},
    {Format::RG32Float, nullptr, nullptr, nullptr, packFloat32<2>, unpackFloat32<2>},

    {Format::RGB32Uint, packInt32<3>, unpackInt32<3>, clampNoOp, nullptr, nullptr},
    {Format::RGB32Sint, packInt32<3>, unpackInt32<3>, clampNoOp, nullptr, nullptr},
    {Format::RGB32Float, nullptr, nullptr, nullptr, packFloat32<3>, unpackFloat32<3>},

    {Format::RGBA32Uint, packInt32<4>, unpackInt32<4>, clampNoOp, nullptr, nullptr},
    {Format::RGBA32Sint, packInt32<4>, unpackInt32<4>, clampNoOp, nullptr, nullptr},
    {Format::RGBA32Float, nullptr, nullptr, nullptr, packFloat32<4>, unpackFloat32<4>},

    {Format::R64Uint, nullptr, nullptr, nullptr, nullptr},
    {Format::R64Sint, nullptr, nullptr, nullptr, nullptr},

    {Format::BGRA4Unorm, nullptr, nullptr, nullptr, packBGRA4Unorm, unpackBGRA4Unorm},
    {Format::B5G6R5Unorm, nullptr, nullptr, nullptr, packB5G6R5Unorm, unpackB5G6R5Unorm},
    {Format::BGR5A1Unorm, nullptr, nullptr, nullptr, packBGR5A1Unorm, unpackBGR5A1Unorm},

    {Format::RGB9E5Ufloat, nullptr, nullptr, nullptr, nullptr, nullptr},
    {Format::RGB10A2Uint, packRGB10A2Uint, unpackRGB10A2Uint, clampRGB10A2Uint, nullptr, nullptr},
    {Format::RGB10A2Unorm, nullptr, nullptr, nullptr, packRGB10A2Unorm, unpackRGB10A2Unorm},
    {Format::R11G11B10Float, nullptr, nullptr, nullptr, nullptr, nullptr},

    {Format::D32Float, nullptr, nullptr, nullptr, packFloat32<1>, unpackFloat32<1>},
    {Format::D16Unorm, nullptr, nullptr, nullptr, packUnorm16<1>, unpackUnorm16<1>},
    {Format::D32FloatS8Uint, nullptr, nullptr, nullptr, nullptr, nullptr},
};

FormatConversionFuncs getFormatConversionFuncs(Format format)
{
    SLANG_RHI_ASSERT(size_t(format) < size_t(Format::_Count));
    SLANG_RHI_ASSERT(sFuncs[size_t(format)].format == format);
    return sFuncs[size_t(format)];
}

} // namespace rhi
