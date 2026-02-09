#pragma once

#include <array>
#include <utility>

namespace rhi {

/// Given a mapping function, create a reverse map from To to From.
/// The mapping function func must be bijective.
/// The reverse map will return defaultValue if the value is not found.
template<typename From, typename To, From min, From max, typename Func>
auto reverseMap(Func func, From defaultValue = From(0))
{
    constexpr size_t size = size_t(max) - size_t(min) + 1;
    static std::array<std::pair<To, From>, size> data = [&]()
    {
        std::array<std::pair<To, From>, size> arr{};
        for (size_t i = 0; i < size; i++)
        {
            arr[i] = {func(From(int(min) + int(i))), From(int(min) + int(i))};
        }
        return arr;
    }();
    return [defaultValue](To value) -> From
    {
        for (const auto& [k, v] : data)
            if (k == value)
                return v;
        return defaultValue;
    };
}

} // namespace rhi
