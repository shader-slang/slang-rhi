#pragma once

// for std::max, std::clamp
#include <algorithm>
#include <array>
#include <tuple>

#include "smart-pointer.h"
#include "com-object.h"
#include "platform.h"
#include "blob.h"
#include "span.h"

namespace rhi {

using Index = int;

template <typename T>
inline T&& _Move(T & obj)
{
	return static_cast<T&&>(obj);
}

template <class T>
inline void hash_combine(std::size_t& seed, const T& v)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed<<6) + (seed>>2);
}

inline uint32_t hash_data(const void* data, size_t size)
{
	uint32_t hash = 0;
	const uint8_t* buf = static_cast<const uint8_t*>(data);
	for (size_t i = 0; i < size; ++i)
	{
		hash = uint32_t(buf[i]) + (hash << 6) + (hash << 16) - hash;
	}
	return hash;
}

template<typename ...Args>
auto make_array(Args ...args)
{
    using T = std::tuple_element_t<0, std::tuple<Args...>>;
    std::array<T, sizeof...(args)> result({args...});
    return result;
}

    namespace math {
        template <typename T>
        inline T getLowestBit(T val)
        {
            return val & (-val);
        }

		inline unsigned int ones32(unsigned int x)
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
			return(x & 0x0000003f);
		}

		inline unsigned int log2Floor(unsigned int x)
		{
			x |= (x >> 1);
			x |= (x >> 2);
			x |= (x >> 4);
			x |= (x >> 8);
			x |= (x >> 16);
			return(ones32(x >> 1));
		}

        union FloatIntUnion
        {
            float fvalue;
            int ivalue;

            SLANG_FORCE_INLINE static FloatIntUnion makeFromInt(int i) { FloatIntUnion cast; cast.ivalue = i; return cast; }
            SLANG_FORCE_INLINE static FloatIntUnion makeFromFloat(float f) { FloatIntUnion cast; cast.fvalue = f; return cast; }
        };
        union DoubleInt64Union
        {
            double dvalue;
            int64_t ivalue;
            SLANG_FORCE_INLINE static DoubleInt64Union makeFromInt64(int64_t i) { DoubleInt64Union cast; cast.ivalue = i; return cast; }
            SLANG_FORCE_INLINE static DoubleInt64Union makeFromDouble(double d) { DoubleInt64Union cast; cast.dvalue = d; return cast; }
        };
        		

		inline float halfToFloat(unsigned short input)
		{
			static const auto magic = FloatIntUnion::makeFromInt((127 + (127 - 15)) << 23);
			static const auto was_infnan = FloatIntUnion::makeFromInt((127 + 16) << 23);
			FloatIntUnion o;
			o.ivalue = (input & 0x7fff) << 13;     // exponent/mantissa bits
			o.fvalue *= magic.fvalue;                 // exponent adjust
			if (o.fvalue >= was_infnan.fvalue)        // make sure Inf/NaN survive
				o.ivalue |= 255 << 23;
			o.ivalue |= (input & 0x8000) << 16;    // sign bit
			return o.fvalue;
		}		

    } // namespace math
} // namespace rhi