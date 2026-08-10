#pragma once

#include <type_traits>

namespace rhi {

/// Given a forward mapping function, perform a reverse lookup from To to From.
/// Iterates over all values in [min, max) and returns the first From whose forward mapping equals value.
/// Returns defaultValue if no match is found.
template<typename From, typename To, From min, From max, typename Func>
From reverseMapLookup(Func func, To value, From defaultValue = From(0))
{
    using U = std::underlying_type_t<From>;
    for (U i = static_cast<U>(min); i < static_cast<U>(max); i++)
    {
        if (func(static_cast<From>(i)) == value)
            return static_cast<From>(i);
    }
    return defaultValue;
}

} // namespace rhi
