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

} // namespace rhi::string
