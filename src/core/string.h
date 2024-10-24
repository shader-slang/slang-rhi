#pragma once

#include <string>
#include <string_view>

namespace rhi::string {

inline std::wstring to_wstring(std::string_view str)
{
    std::wstring wstr;
    wstr.assign(str.begin(), str.end());
    return wstr;
}

inline std::string from_wstring(std::wstring_view wstr)
{
    std::string str(wstr.size(), '\0');
    for (size_t i = 0; i < wstr.size(); i++)
    {
        str[i] = static_cast<char>(wstr[i]);
    }
    return str;
}

inline bool ends_with(std::string_view str, std::string_view suffix)
{
    if (str.size() < suffix.size())
    {
        return false;
    }
    return str.substr(str.size() - suffix.size()) == suffix;
}

inline std::string from_cstr(const char* str)
{
    return str ? str : "";
}

namespace detail {
template<typename From, typename To = From>
inline To convertArg(const From& arg)
{
    return arg;
}
inline const char* convertArg(const std::string& arg)
{
    return arg.c_str();
}
} // namespace detail

template<typename... Args>
std::string format(const char* format, Args&&... args)
{
    size_t len = snprintf(nullptr, 0, format, detail::convertArg(args)...);
    std::string str;
    str.resize(len);
    snprintf(str.data(), len + 1, format, detail::convertArg(args)...);
    return str;
}

} // namespace rhi::string
