#pragma once

// #define SLANG_ASSERT(x)

// for std::max, std::clamp
#include <algorithm>
#include <array>
#include <tuple>

// DONE
// #include "core/slang-basic.h"
// #include "core/slang-io.h"
// #include "core/slang-short-list.h"
// #include "core/slang-blob.h"
// #include "core/slang-math.h"

// TO BE DONE
#include "core/slang-smart-pointer.h"
#include "core/slang-chunked-list.h"
#include "core/slang-com-object.h"
#include "core/slang-persistent-cache.h"
#include "core/slang-platform.h"
#include "core/slang-virtual-object-pool.h"

namespace Slang {}

namespace gfx {

template<typename ...Args>
auto make_array(Args ...args)
{
    using T = std::tuple_element_t<0, std::tuple<Args...>>;
    std::array<T, sizeof...(args)> result({args...});
    return result;
}

    namespace math {
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

    } // namespace math
} // namespace gfx