#pragma once

#include "core/common.h"

#include <cmath>

namespace rhi {

/// Packs up to 4 integers to a packed format. Inputs are truncated to the format's range.
using PackIntFunc = void (*)(const uint32_t[4], void*);
/// Unpacks a packed format to up to 4 integers.
using UnpackIntFunc = void (*)(const void*, uint32_t[4]);
/// Packs up to 4 floats to a packed format. Inputs are clamped to the format's range.
using PackFloatFunc = void (*)(const float[4], void*);
/// Unpacks a packed format to up to 4 floats.
using UnpackFloatFunc = void (*)(const void*, float[4]);

struct FormatConversionFuncs
{
    Format format;
    PackIntFunc packIntFunc;
    UnpackIntFunc unpackIntFunc;
    PackFloatFunc packFloatFunc;
    UnpackFloatFunc unpackFloatFunc;
};

FormatConversionFuncs getFormatConversionFuncs(Format format);

template<size_t N>
inline void packInt8(const uint32_t in[4], void* out)
{
    for (size_t i = 0; i < N; ++i)
        reinterpret_cast<uint8_t*>(out)[i] = clamp(in[i], 0u, 255u);
}

template<size_t N>
inline void unpackInt8(const void* in, uint32_t out[4])
{
    for (size_t i = 0; i < N; ++i)
        out[i] = reinterpret_cast<const uint8_t*>(in)[i];
}

template<size_t N>
inline void packInt16(const uint32_t in[4], void* out)
{
    for (size_t i = 0; i < N; ++i)
        reinterpret_cast<uint16_t*>(out)[i] = clamp(in[i], 0u, 65535u);
}

template<size_t N>
inline void unpackInt16(const void* in, uint32_t out[4])
{
    for (size_t i = 0; i < N; ++i)
        out[i] = reinterpret_cast<const uint16_t*>(in)[i];
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

inline void packInt4444(const uint32_t in[4], void* out)
{
    uint16_t packed = 0;
    packed |= clamp(in[0], 0u, 15u);
    packed |= clamp(in[1], 0u, 15u) << 4;
    packed |= clamp(in[2], 0u, 15u) << 8;
    packed |= clamp(in[3], 0u, 15u) << 12;
    *reinterpret_cast<uint16_t*>(out) = packed;
}

inline void unpackInt4444(const void* in, uint32_t out[4])
{
    uint16_t packed = *reinterpret_cast<const uint16_t*>(in);
    out[0] = packed & 0xf;
    out[1] = (packed >> 4) & 0xf;
    out[2] = (packed >> 8) & 0xf;
    out[3] = (packed >> 12) & 0xf;
}

inline void packInt565(const uint32_t in[4], void* out)
{
    uint16_t packed = 0;
    packed |= clamp(in[0], 0u, 31u);
    packed |= clamp(in[1], 0u, 63u) << 5;
    packed |= clamp(in[2], 0u, 31u) << 11;
    *reinterpret_cast<uint16_t*>(out) = packed;
}

inline void unpackInt565(const void* in, uint32_t out[4])
{
    uint16_t packed = *reinterpret_cast<const uint16_t*>(in);
    out[0] = packed & 0x1f;
    out[1] = (packed >> 5) & 0x3f;
    out[2] = (packed >> 11) & 0x1f;
}

inline void packInt5551(const uint32_t in[4], void* out)
{
    uint16_t packed = 0;
    packed |= clamp(in[0], 0u, 31u);
    packed |= clamp(in[1], 0u, 31u) << 5;
    packed |= clamp(in[2], 0u, 31u) << 10;
    packed |= clamp(in[3], 0u, 1u) << 15;
    *reinterpret_cast<uint16_t*>(out) = packed;
}

inline void unpackInt5551(const void* in, uint32_t out[4])
{
    uint16_t packed = *reinterpret_cast<const uint16_t*>(in);
    out[0] = packed & 0x1f;
    out[1] = (packed >> 5) & 0x1f;
    out[2] = (packed >> 10) & 0x1f;
    out[3] = (packed >> 15) & 0x1;
}

inline void packInt10_10_10_2(const uint32_t in[4], void* out)
{
    uint32_t packed = 0;
    packed |= clamp(in[0], 0u, 1023u);
    packed |= clamp(in[1], 0u, 1023u) << 10;
    packed |= clamp(in[2], 0u, 1023u) << 20;
    packed |= clamp(in[3], 0u, 3u) << 30;
    *reinterpret_cast<uint32_t*>(out) = packed;
}

inline void unpackInt10_10_10_2(const void* in, uint32_t out[4])
{
    uint32_t packed = *reinterpret_cast<const uint32_t*>(in);
    out[0] = packed & 0x3ff;
    out[1] = (packed >> 10) & 0x3ff;
    out[2] = (packed >> 20) & 0x3ff;
    out[3] = (packed >> 30) & 0x3;
}

inline void packInt11_11_10(const uint32_t in[4], void* out)
{
    uint32_t packed = 0;
    packed |= clamp(in[0], 0u, 2047u);
    packed |= clamp(in[1], 0u, 2047u) << 11;
    packed |= clamp(in[2], 0u, 1023u) << 22;
    *reinterpret_cast<uint32_t*>(out) = packed;
}

inline void unpackInt11_11_10(const void* in, uint32_t out[4])
{
    uint32_t packed = *reinterpret_cast<const uint32_t*>(in);
    out[0] = packed & 0x7ff;
    out[1] = (packed >> 11) & 0x7ff;
    out[2] = (packed >> 22) & 0x3ff;
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

inline void packUnormBGRA8(const float in[4], void* out)
{
    reinterpret_cast<uint8_t*>(out)[0] = uint8_t(::floor(in[2] * 255.f + 0.5f));
    reinterpret_cast<uint8_t*>(out)[1] = uint8_t(::floor(in[1] * 255.f + 0.5f));
    reinterpret_cast<uint8_t*>(out)[2] = uint8_t(::floor(in[0] * 255.f + 0.5f));
    reinterpret_cast<uint8_t*>(out)[3] = uint8_t(::floor(in[3] * 255.f + 0.5f));
}

inline void unpackUnormBGRA8(const void* in, float out[4])
{
    out[0] = reinterpret_cast<const uint8_t*>(in)[2] / 255.f;
    out[1] = reinterpret_cast<const uint8_t*>(in)[1] / 255.f;
    out[2] = reinterpret_cast<const uint8_t*>(in)[0] / 255.f;
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

inline void packUnorm4444(const float in[4], void* out)
{
    uint16_t packed = 0;
    packed |= uint16_t(::floor(in[0] * 15.f + 0.5f));
    packed |= uint16_t(::floor(in[1] * 15.f + 0.5f)) << 4;
    packed |= uint16_t(::floor(in[2] * 15.f + 0.5f)) << 8;
    packed |= uint16_t(::floor(in[3] * 15.f + 0.5f)) << 12;
    *reinterpret_cast<uint16_t*>(out) = packed;
}

inline void unpackUnorm4444(const void* in, float out[4])
{
    uint16_t packed = *reinterpret_cast<const uint16_t*>(in);
    out[0] = (packed & 0xf) / 15.f;
    out[1] = ((packed >> 4) & 0xf) / 15.f;
    out[2] = ((packed >> 8) & 0xf) / 15.f;
    out[3] = ((packed >> 12) & 0xf) / 15.f;
}

inline void packUnorm565(const float in[4], void* out)
{
    uint16_t packed = 0;
    packed |= uint16_t(::floor(in[0] * 31.f + 0.5f));
    packed |= uint16_t(::floor(in[1] * 63.f + 0.5f)) << 5;
    packed |= uint16_t(::floor(in[2] * 31.f + 0.5f)) << 11;
    *reinterpret_cast<uint16_t*>(out) = packed;
}

inline void unpackUnorm565(const void* in, float out[4])
{
    uint16_t packed = *reinterpret_cast<const uint16_t*>(in);
    out[0] = (packed & 0x1f) / 31.f;
    out[1] = ((packed >> 5) & 0x3f) / 63.f;
    out[2] = ((packed >> 11) & 0x1f) / 31.f;
}

inline void packUnorm5551(const float in[4], void* out)
{
    uint16_t packed = 0;
    packed |= uint16_t(::floor(in[0] * 31.f + 0.5f));
    packed |= uint16_t(::floor(in[1] * 31.f + 0.5f)) << 5;
    packed |= uint16_t(::floor(in[2] * 31.f + 0.5f)) << 10;
    packed |= uint16_t(::floor(in[3] + 0.5f)) << 15;
    *reinterpret_cast<uint16_t*>(out) = packed;
}

inline void unpackUnorm5551(const void* in, float out[4])
{
    uint16_t packed = *reinterpret_cast<const uint16_t*>(in);
    out[0] = (packed & 0x1f) / 31.f;
    out[1] = ((packed >> 5) & 0x1f) / 31.f;
    out[2] = ((packed >> 10) & 0x1f) / 31.f;
    out[3] = ((packed >> 15) & 0x1);
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
