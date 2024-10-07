#pragma once

namespace rhi {

#include <unordered_map>

/// Given a mapping function, create a reverse map from To to From.
/// The mapping function func must be bijective.
/// The reverse map will return defaultValue if the value is not found.
template<typename From, typename To, typename Func>
auto reverseMap(Func func, From min, From max, From defaultValue = From(0))
{
    static std::unordered_map<To, From> reverseMap = [&]()
    {
        std::unordered_map<To, From> map;
        for (int i = int(min); i <= int(max); i++)
        {
            map[func(From(i))] = From(i);
        }
        return map;
    }();
    return [defaultValue](To value) -> From
    {
        auto it = reverseMap.find(value);
        return it == reverseMap.end() ? defaultValue : it->second;
    };
}

} // namespace rhi
