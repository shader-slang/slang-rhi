#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <initializer_list>
#include <mutex>

#include <cstring>
#include <cstdint>

#include "blob.h"
#include "com-object.h"
#include "platform.h"
#include "smart-pointer.h"
#include "span.h"
#include "string.h"
#include "struct-holder.h"

namespace rhi {

// A type cast that is safer than static_cast in debug builds, and is a simple static_cast in release builds.
// Used mostly for downcasting IFoo* pointers to their implementation classes in the backends.
template<typename T, typename U>
T checked_cast(U u)
{
    static_assert(!std::is_same<T, U>::value, "Redundant checked_cast");
#if SLANG_RHI_DEBUG
    if (!u)
        return nullptr;
    T t = dynamic_cast<T>(u);
    if (!t)
        SLANG_RHI_ASSERT_FAILURE("Invalid type cast");
    return t;
#else
    return static_cast<T>(u);
#endif
}

template<typename T>
inline bool contains(const T* array, size_t count, T value)
{
    for (size_t i = 0; i < count; ++i)
        if (array[i] == value)
            return true;
    return false;
}

template<typename T>
constexpr T min(T a, T b)
{
    return a < b ? a : b;
}

template<typename T>
T min(std::initializer_list<T> list)
{
    T result = *list.begin();
    for (auto& v : list)
        result = min(result, v);
    return result;
}

template<typename T>
constexpr T max(T a, T b)
{
    return a > b ? a : b;
}

template<typename T>
T max(std::initializer_list<T> list)
{
    T result = *list.begin();
    for (auto& v : list)
        result = max(result, v);
    return result;
}

template<typename T>
constexpr T clamp(T v, T lo, T hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

template<typename T>
inline T&& _Move(T& obj)
{
    return static_cast<T&&>(obj);
}

template<class T>
inline void hash_combine(std::size_t& seed, const T& v)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

namespace math {
template<typename T>
inline T getLowestBit(T val)
{
    return val & (-val);
}

inline bool isPowerOf2(size_t value)
{
    return (value != 0) && ((value & (value - 1)) == 0);
}

/// Perform integer rounded up division.
inline size_t divideRoundedUp(size_t numerator, size_t denominator)
{
    return (numerator + denominator - 1) / denominator;
}

/// Calculate size taking into account alignment.
inline size_t calcAligned(size_t size, size_t alignment)
{
    return divideRoundedUp(size, alignment) * alignment;
}

/// More optimal calculate size taking into account alignment that only supports power of 2.
inline size_t calcAligned2(size_t size, size_t alignment)
{
    SLANG_RHI_ASSERT(isPowerOf2(alignment));
    return (size + alignment - 1) & ~(alignment - 1);
}

inline uint32_t ones32(uint32_t x)
{
    /* 32-bit recursive reduction using SWAR...
        but first step is mapping 2-bit values
        into sum of 2 1-bit values in sneaky way
    */
    x -= ((x >> 1) & 0x55555555);
    x = (((x >> 2) & 0x33333333) + (x & 0x33333333));
    x = (((x >> 4) + x) & 0x0f0f0f0f);
    x += (x >> 8);
    x += (x >> 16);
    return (x & 0x0000003f);
}

inline uint32_t log2Floor(uint32_t x)
{
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);
    return (ones32(x >> 1));
}

union FloatIntUnion
{
    float fvalue;
    int ivalue;

    SLANG_FORCE_INLINE static FloatIntUnion makeFromInt(int i)
    {
        FloatIntUnion cast;
        cast.ivalue = i;
        return cast;
    }
    SLANG_FORCE_INLINE static FloatIntUnion makeFromFloat(float f)
    {
        FloatIntUnion cast;
        cast.fvalue = f;
        return cast;
    }
};
union DoubleInt64Union
{
    double dvalue;
    int64_t ivalue;
    SLANG_FORCE_INLINE static DoubleInt64Union makeFromInt64(int64_t i)
    {
        DoubleInt64Union cast;
        cast.ivalue = i;
        return cast;
    }
    SLANG_FORCE_INLINE static DoubleInt64Union makeFromDouble(double d)
    {
        DoubleInt64Union cast;
        cast.dvalue = d;
        return cast;
    }
};

inline uint16_t floatToHalf(float value)
{
    int i = FloatIntUnion::makeFromFloat(value).ivalue;

    // Our floating point number, f, is represented by the bit
    // pattern in integer i.  Disassemble that bit pattern into
    // the sign, s, the exponent, e, and the significand, m.
    // Shift s into the position where it will go in the
    // resulting half number.
    // Adjust e, accounting for the different exponent bias
    // of float and half (127 versus 15).
    int s = (i >> 16) & 0x00008000;
    int e = ((i >> 23) & 0x000000ff) - (127 - 15);
    int m = i & 0x007fffff;

    // Now reassemble s, e and m into a half:
    if (e <= 0)
    {
        if (e < -10)
        {
            // E is less than -10.  The absolute value of f is
            // less than half_MIN (f may be a small normalized
            // float, a denormalized float or a zero).
            // We convert f to a half zero.
            return uint16_t(s);
        }

        // E is between -10 and 0.  F is a normalized float,
        // whose magnitude is less than __half_NRM_MIN.
        // We convert f to a denormalized half.
        m = (m | 0x00800000) >> (1 - e);

        // Round to nearest, round "0.5" up.
        // Rounding may cause the significand to overflow and make
        // our number normalized.  Because of the way a half's bits
        // are laid out, we don't have to treat this case separately;
        // the code below will handle it correctly.
        if (m & 0x00001000)
        {
            m += 0x00002000;
        }

        // Assemble the half from s, e (zero) and m.
        return uint16_t(s | (m >> 13));
    }
    else if (e == 0xff - (127 - 15))
    {
        if (m == 0)
        {
            // F is an infinity; convert f to a half
            // infinity with the same sign as f.
            return uint16_t(s | 0x7c00);
        }
        else
        {
            // F is a NAN; we produce a half NAN that preserves
            // the sign bit and the 10 leftmost bits of the
            // significand of f, with one exception: If the 10
            // leftmost bits are all zero, the NAN would turn
            // into an infinity, so we have to set at least one
            // bit in the significand.
            m >>= 13;
            return uint16_t(s | 0x7c00 | m | (m == 0));
        }
    }
    else
    {
        // E is greater than zero.  F is a normalized float.
        // We try to convert f to a normalized half.

        // Round to nearest, round "0.5" up
        if (m & 0x00001000)
        {
            m += 0x00002000;

            if (m & 0x00800000)
            {
                m = 0;  // overflow in significand,
                e += 1; // adjust exponent
            }
        }

        // Handle exponent overflow
        if (e > 30)
        {
            // overflow(); // Cause a hardware floating point overflow;
            return uint16_t(s | 0x7c00); // Return infinity with same sign as f.
        }

        // Assemble the half from s, e and m.
        return uint16_t(s | (e << 10) | (m >> 13));
    }
}

inline float halfToFloat(uint16_t value)
{
    int s = (value >> 15) & 0x00000001;
    int e = (value >> 10) & 0x0000001f;
    int m = value & 0x000003ff;

    if (e == 0)
    {
        if (m == 0)
        {
            // Plus or minus zero
            return FloatIntUnion::makeFromInt(s << 31).fvalue;
        }
        else
        {
            // Denormalized number -- renormalize it
            while (!(m & 0x00000400))
            {
                m <<= 1;
                e -= 1;
            }

            e += 1;
            m &= ~0x00000400;
        }
    }
    else if (e == 31)
    {
        if (m == 0)
        {
            // Positive or negative infinity
            return FloatIntUnion::makeFromInt((s << 31) | 0x7f800000).fvalue;
        }
        else
        {
            // Nan -- preserve sign and significand bits
            return FloatIntUnion::makeFromInt((s << 31) | 0x7f800000 | (m << 13)).fvalue;
        }
    }

    // Normalized number
    e = e + (127 - 15);
    m = m << 13;

    // Assemble s, e and m.
    return FloatIntUnion::makeFromInt((s << 31) | (e << 23) | m).fvalue;
}

} // namespace math
} // namespace rhi
