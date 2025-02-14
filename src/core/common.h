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
#ifdef _DEBUG
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

/// Calculate size taking into account alignment. Alignment must be a power of 2
inline size_t calcAligned(size_t size, size_t alignment)
{
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

inline float halfToFloat(unsigned short input)
{
    static const auto magic = FloatIntUnion::makeFromInt((127 + (127 - 15)) << 23);
    static const auto was_infnan = FloatIntUnion::makeFromInt((127 + 16) << 23);
    FloatIntUnion o;
    o.ivalue = (input & 0x7fff) << 13; // exponent/mantissa bits
    o.fvalue *= magic.fvalue;          // exponent adjust
    if (o.fvalue >= was_infnan.fvalue) // make sure Inf/NaN survive
        o.ivalue |= 255 << 23;
    o.ivalue |= (input & 0x8000) << 16; // sign bit
    return o.fvalue;
}

} // namespace math
} // namespace rhi
