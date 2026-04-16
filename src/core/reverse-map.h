#pragma once

namespace rhi {

/// Given a forward mapping function, perform a reverse lookup from To to From.
/// Iterates over all values in [min, max) and returns the first From whose forward mapping equals value.
/// Returns defaultValue if no match is found.
template<typename From, typename To, From min, From max, typename Func>
From reverseMapLookup(Func func, To value, From defaultValue = From(0))
{
    for (int i = int(min); i < int(max); i++)
    {
        if (func(From(i)) == value)
            return From(i);
    }
    return defaultValue;
}

} // namespace rhi
