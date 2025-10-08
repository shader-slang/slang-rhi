#pragma once

#include "core/common.h"

#include <cmath>

namespace rhi {

/// Packs up to 4 32-bit integers to a packed format.
/// For unsigned formats, the input is treated as unsigned.
/// For signed formats, the input is treated as signed.
using PackIntFunc = void (*)(const uint32_t[4], void*);

/// Unpacks a packed format to up to 4 32-bit integers.
/// For unsigned formats, the output is treated as unsigned.
/// For signed formats, the output is treated as signed.
using UnpackIntFunc = void (*)(const void*, uint32_t[4]);

/// Clamps up to 4 32-bit integers to the format's range.
/// For unsigned formats, the input is treated as unsigned.
/// For signed formats, the input is treated as signed.
using ClampIntFunc = void (*)(uint32_t[4]);

/// Packs up to 4 floats to a packed format.
/// Inputs are clamped to the format's range.
using PackFloatFunc = void (*)(const float[4], void*);

/// Unpacks a packed format to up to 4 floats.
using UnpackFloatFunc = void (*)(const void*, float[4]);

struct FormatConversionFuncs
{
    Format format;
    /// Packs up to 4 32-bit integers to a packed format (available for integer formats only).
    PackIntFunc packIntFunc;
    /// Unpacks a packed format to up to 4 32-bit integers (available for integer formats only).
    UnpackIntFunc unpackIntFunc;
    /// Clamps up to 4 32-bit integers to the format's range (available for integer formats only).
    ClampIntFunc clampIntFunc;
    /// Packs up to 4 floats to a packed format (available for float and normalized formats only).
    PackFloatFunc packFloatFunc;
    /// Unpacks a packed format to up to 4 floats (available for float and normalized formats only).
    UnpackFloatFunc unpackFloatFunc;
};

FormatConversionFuncs getFormatConversionFuncs(Format format);

template<size_t N>
inline void packUint8(const uint32_t in[4], void* out)
{
    for (size_t i = 0; i < N; ++i)
        reinterpret_cast<uint8_t*>(out)[i] = in[i] & 0xff;
}

template<size_t N>
inline void unpackUint8(const void* in, uint32_t out[4])
{
    for (size_t i = 0; i < N; ++i)
        out[i] = reinterpret_cast<const uint8_t*>(in)[i];
}

template<size_t N>
inline void clampUint8(uint32_t inout[4])
{
    for (size_t i = 0; i < N; ++i)
        inout[i] = clamp(inout[i], 0u, 255u);
}

template<size_t N>
inline void packSint8(const uint32_t in[4], void* out)
{
    for (size_t i = 0; i < N; ++i)
        reinterpret_cast<int8_t*>(out)[i] = static_cast<int32_t>(in[i]) & 0xff;
}

template<size_t N>
inline void unpackSint8(const void* in, uint32_t out[4])
{
    for (size_t i = 0; i < N; ++i)
        out[i] = reinterpret_cast<const int8_t*>(in)[i];
}

template<size_t N>
inline void clampSint8(uint32_t inout[4])
{
    for (size_t i = 0; i < N; ++i)
        inout[i] = clamp(static_cast<int32_t>(inout[i]), -128, 127);
}

template<size_t N>
inline void packUint16(const uint32_t in[4], void* out)
{
    for (size_t i = 0; i < N; ++i)
        reinterpret_cast<uint16_t*>(out)[i] = in[i] & 0xffff;
}

template<size_t N>
inline void unpackUint16(const void* in, uint32_t out[4])
{
    for (size_t i = 0; i < N; ++i)
        out[i] = reinterpret_cast<const uint16_t*>(in)[i];
}

template<size_t N>
inline void clampUint16(uint32_t inout[4])
{
    for (size_t i = 0; i < N; ++i)
        inout[i] = clamp(inout[i], 0u, 65535u);
}

template<size_t N>
inline void clampSint16(uint32_t inout[4])
{
    for (size_t i = 0; i < N; ++i)
        inout[i] = clamp(static_cast<int32_t>(inout[i]), -32768, 32767);
}

template<size_t N>
inline void packSint16(const uint32_t in[4], void* out)
{
    for (size_t i = 0; i < N; ++i)
        reinterpret_cast<int16_t*>(out)[i] = static_cast<int32_t>(in[i]) & 0xffff;
}

template<size_t N>
inline void unpackSint16(const void* in, uint32_t out[4])
{
    for (size_t i = 0; i < N; ++i)
        out[i] = reinterpret_cast<const int16_t*>(in)[i];
}

template<size_t N>
inline void packInt32(const uint32_t in[4], void* out)
{
    for (size_t i = 0; i < N; ++i)
        reinterpret_cast<uint32_t*>(out)[i] = in[i];
}

template<size_t N>
inline void unpackInt32(const void* in, uint32_t out[4])
{
    for (size_t i = 0; i < N; ++i)
        out[i] = reinterpret_cast<const uint32_t*>(in)[i];
}

inline void packRGB10A2Uint(const uint32_t in[4], void* out)
{
    uint32_t packed = 0;
    packed |= (in[0] & 0x3ff);
    packed |= (in[1] & 0x3ff) << 10;
    packed |= (in[2] & 0x3ff) << 20;
    packed |= (in[3] & 0x3) << 30;
    *reinterpret_cast<uint32_t*>(out) = packed;
}

inline void unpackRGB10A2Uint(const void* in, uint32_t out[4])
{
    uint32_t packed = *reinterpret_cast<const uint32_t*>(in);
    out[0] = packed & 0x3ff;
    out[1] = (packed >> 10) & 0x3ff;
    out[2] = (packed >> 20) & 0x3ff;
    out[3] = (packed >> 30) & 0x3;
}

inline void clampRGB10A2Uint(uint32_t inout[4])
{
    inout[0] = clamp(inout[0], 0u, 1023u);
    inout[1] = clamp(inout[1], 0u, 1023u);
    inout[2] = clamp(inout[2], 0u, 1023u);
    inout[3] = clamp(inout[3], 0u, 3u);
}

template<size_t N>
inline void packUnorm8(const float in[4], void* out)
{
    for (size_t i = 0; i < N; ++i)
        reinterpret_cast<uint8_t*>(out)[i] = uint8_t(::floor(in[i] * 255.f + 0.5f));
}

template<size_t N>
inline void unpackUnorm8(const void* in, float out[4])
{
    for (size_t i = 0; i < N; ++i)
        out[i] = reinterpret_cast<const uint8_t*>(in)[i] / 255.f;
}

inline float linearToSrgb(float v)
{
    if (v <= 0.0031308f)
        return v * 12.92f;
    else
        return ::pow(v, 1.f / 2.4f) * 1.055f - 0.055f;
}

inline float srgbToLinear(float v)
{
    if (v <= 0.04045f)
        return v * (1.f / 12.92f);
    else
        return ::pow((v + 0.055f) * (1.f / 1.055f), 2.4f);
}

inline void packRGBA8UnormSrgb(const float in[4], void* out)
{
    reinterpret_cast<uint8_t*>(out)[0] = uint8_t(::floor(linearToSrgb(in[0]) * 255.f + 0.5f));
    reinterpret_cast<uint8_t*>(out)[1] = uint8_t(::floor(linearToSrgb(in[1]) * 255.f + 0.5f));
    reinterpret_cast<uint8_t*>(out)[2] = uint8_t(::floor(linearToSrgb(in[2]) * 255.f + 0.5f));
    reinterpret_cast<uint8_t*>(out)[3] = uint8_t(::floor(in[3] * 255.f + 0.5f));
}

inline void unpackRGBA8UnormSrgb(const void* in, float out[4])
{
    out[0] = srgbToLinear(reinterpret_cast<const uint8_t*>(in)[0] / 255.f);
    out[1] = srgbToLinear(reinterpret_cast<const uint8_t*>(in)[1] / 255.f);
    out[2] = srgbToLinear(reinterpret_cast<const uint8_t*>(in)[2] / 255.f);
    out[3] = reinterpret_cast<const uint8_t*>(in)[3] / 255.f;
}

inline void packBGRA8Unorm(const float in[4], void* out)
{
    reinterpret_cast<uint8_t*>(out)[0] = uint8_t(::floor(in[2] * 255.f + 0.5f));
    reinterpret_cast<uint8_t*>(out)[1] = uint8_t(::floor(in[1] * 255.f + 0.5f));
    reinterpret_cast<uint8_t*>(out)[2] = uint8_t(::floor(in[0] * 255.f + 0.5f));
    reinterpret_cast<uint8_t*>(out)[3] = uint8_t(::floor(in[3] * 255.f + 0.5f));
}

inline void unpackBGRA8Unorm(const void* in, float out[4])
{
    out[0] = reinterpret_cast<const uint8_t*>(in)[2] / 255.f;
    out[1] = reinterpret_cast<const uint8_t*>(in)[1] / 255.f;
    out[2] = reinterpret_cast<const uint8_t*>(in)[0] / 255.f;
    out[3] = reinterpret_cast<const uint8_t*>(in)[3] / 255.f;
}

inline void packBGRA8UnormSrgb(const float in[4], void* out)
{
    reinterpret_cast<uint8_t*>(out)[0] = uint8_t(::floor(linearToSrgb(in[2]) * 255.f + 0.5f));
    reinterpret_cast<uint8_t*>(out)[1] = uint8_t(::floor(linearToSrgb(in[1]) * 255.f + 0.5f));
    reinterpret_cast<uint8_t*>(out)[2] = uint8_t(::floor(linearToSrgb(in[0]) * 255.f + 0.5f));
    reinterpret_cast<uint8_t*>(out)[3] = uint8_t(::floor(in[3] * 255.f + 0.5f));
}

inline void unpackBGRA8UnormSrgb(const void* in, float out[4])
{
    out[0] = srgbToLinear(reinterpret_cast<const uint8_t*>(in)[2] / 255.f);
    out[1] = srgbToLinear(reinterpret_cast<const uint8_t*>(in)[1] / 255.f);
    out[2] = srgbToLinear(reinterpret_cast<const uint8_t*>(in)[0] / 255.f);
    out[3] = reinterpret_cast<const uint8_t*>(in)[3] / 255.f;
}

template<size_t N>
inline void packUnorm16(const float in[4], void* out)
{
    for (size_t i = 0; i < N; ++i)
        reinterpret_cast<uint16_t*>(out)[i] = uint16_t(::floor(in[i] * 65535.f + 0.5f));
}

template<size_t N>
inline void unpackUnorm16(const void* in, float out[4])
{
    for (size_t i = 0; i < N; ++i)
        out[i] = reinterpret_cast<const uint16_t*>(in)[i] / 65535.f;
}

template<size_t N>
inline void packSnorm8(const float in[4], void* out)
{
    for (size_t i = 0; i < N; ++i)
        reinterpret_cast<int8_t*>(out)[i] = int8_t(::floor(in[i] * 127.f));
}

template<size_t N>
inline void unpackSnorm8(const void* in, float out[4])
{
    for (size_t i = 0; i < N; ++i)
        out[i] = max(-1.f, reinterpret_cast<const int8_t*>(in)[i] / 127.f);
}

template<size_t N>
inline void packSnorm16(const float in[4], void* out)
{
    for (size_t i = 0; i < N; ++i)
        reinterpret_cast<int16_t*>(out)[i] = int16_t(::floor(in[i] * 32767.f));
}

template<size_t N>
inline void unpackSnorm16(const void* in, float out[4])
{
    for (size_t i = 0; i < N; ++i)
        out[i] = max(-1.f, reinterpret_cast<const int16_t*>(in)[i] / 32767.f);
}

template<size_t N>
inline void packFloat16(const float in[4], void* out)
{
    for (size_t i = 0; i < N; ++i)
    {
        reinterpret_cast<uint16_t*>(out)[i] = math::floatToHalf(in[i]);
    }
}

template<size_t N>
inline void unpackFloat16(const void* in, float out[4])
{
    for (size_t i = 0; i < N; ++i)
    {
        out[i] = math::halfToFloat(reinterpret_cast<const uint16_t*>(in)[i]);
    }
}

template<size_t N>
inline void packFloat32(const float in[4], void* out)
{
    for (size_t i = 0; i < N; ++i)
        reinterpret_cast<float*>(out)[i] = in[i];
}

template<size_t N>
inline void unpackFloat32(const void* in, float out[4])
{
    for (size_t i = 0; i < N; ++i)
        out[i] = reinterpret_cast<const float*>(in)[i];
}

inline void packBGRA4Unorm(const float in[4], void* out)
{
    uint16_t packed = 0;
    packed |= uint16_t(::floor(in[2] * 15.f + 0.5f));
    packed |= uint16_t(::floor(in[1] * 15.f + 0.5f)) << 4;
    packed |= uint16_t(::floor(in[0] * 15.f + 0.5f)) << 8;
    packed |= uint16_t(::floor(in[3] * 15.f + 0.5f)) << 12;
    *reinterpret_cast<uint16_t*>(out) = packed;
}

inline void unpackBGRA4Unorm(const void* in, float out[4])
{
    uint16_t packed = *reinterpret_cast<const uint16_t*>(in);
    out[2] = (packed & 0xf) / 15.f;
    out[1] = ((packed >> 4) & 0xf) / 15.f;
    out[0] = ((packed >> 8) & 0xf) / 15.f;
    out[3] = ((packed >> 12) & 0xf) / 15.f;
}

inline void packB5G6R5Unorm(const float in[4], void* out)
{
    uint16_t packed = 0;
    packed |= uint16_t(::floor(in[2] * 31.f + 0.5f));
    packed |= uint16_t(::floor(in[1] * 63.f + 0.5f)) << 5;
    packed |= uint16_t(::floor(in[0] * 31.f + 0.5f)) << 11;
    *reinterpret_cast<uint16_t*>(out) = packed;
}

inline void unpackB5G6R5Unorm(const void* in, float out[4])
{
    uint16_t packed = *reinterpret_cast<const uint16_t*>(in);
    out[2] = (packed & 0x1f) / 31.f;
    out[1] = ((packed >> 5) & 0x3f) / 63.f;
    out[0] = ((packed >> 11) & 0x1f) / 31.f;
}

inline void packBGR5A1Unorm(const float in[4], void* out)
{
    uint16_t packed = 0;
    packed |= uint16_t(::floor(in[2] * 31.f + 0.5f));
    packed |= uint16_t(::floor(in[1] * 31.f + 0.5f)) << 5;
    packed |= uint16_t(::floor(in[0] * 31.f + 0.5f)) << 10;
    packed |= uint16_t(::floor(in[3] + 0.5f)) << 15;
    *reinterpret_cast<uint16_t*>(out) = packed;
}

inline void unpackBGR5A1Unorm(const void* in, float out[4])
{
    uint16_t packed = *reinterpret_cast<const uint16_t*>(in);
    out[2] = (packed & 0x1f) / 31.f;
    out[1] = ((packed >> 5) & 0x1f) / 31.f;
    out[0] = ((packed >> 10) & 0x1f) / 31.f;
    out[3] = ((packed >> 15) & 0x1);
}

inline void packRGB10A2Unorm(const float in[4], void* out)
{
    uint32_t packed = 0;
    packed |= uint32_t(::floor(in[0] * 1023.f + 0.5f)) & 0x3ff;
    packed |= (uint32_t(::floor(in[1] * 1023.f + 0.5f)) & 0x3ff) << 10;
    packed |= (uint32_t(::floor(in[2] * 1023.f + 0.5f)) & 0x3ff) << 20;
    packed |= (uint32_t(::floor(in[3] * 3.f + 0.5f)) & 0x3) << 30;
    *reinterpret_cast<uint32_t*>(out) = packed;
}

inline void unpackRGB10A2Unorm(const void* in, float out[4])
{
    uint32_t packed = *reinterpret_cast<const uint32_t*>(in);
    out[0] = (packed & 0x3ff) / 1023.f;
    out[1] = ((packed >> 10) & 0x3ff) / 1023.f;
    out[2] = ((packed >> 20) & 0x3ff) / 1023.f;
    out[3] = ((packed >> 30) & 0x3) / 3.f;
}

inline void truncateBySintFormat(Format format, const uint32_t in[4], uint32_t out[4])
{
    switch (format)
    {
    case Format::R8Sint:
    case Format::RG8Sint:
    case Format::RGBA8Sint:
        out[0] = in[0] & 0xff;
        out[1] = in[1] & 0xff;
        out[2] = in[2] & 0xff;
        out[3] = in[3] & 0xff;
        break;
    case Format::R16Sint:
    case Format::RG16Sint:
    case Format::RGBA16Sint:
        out[0] = in[0] & 0xffff;
        out[1] = in[1] & 0xffff;
        out[2] = in[2] & 0xffff;
        out[3] = in[3] & 0xffff;
        break;
    default:
        out[0] = in[0];
        out[1] = in[1];
        out[2] = in[2];
        out[3] = in[3];
        break;
    }
}

} // namespace rhi
