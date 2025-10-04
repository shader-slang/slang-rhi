#pragma once

#include <string>
#include <string_view>

namespace rhi::string {

inline void copy_safe(char* dst, size_t dstSize, const char* src)
{
    if (!dst || !src || dstSize == 0)
    {
        return;
    }

    // Copy characters from src to dst until either (dstSize - 1) is exhausted or we hit a null terminator in src.
    while (dstSize > 1 && *src)
    {
        *dst++ = *src++;
        --dstSize;
    }
    // Fill the rest of dst with null characters to ensure null-termination.
    while (dstSize > 0)
    {
        *dst++ = 0;
        --dstSize;
    }
}

inline void copy_safe(char* dst, size_t dstSize, const char* src, size_t srcSize)
{
    if (!dst || !src || dstSize == 0 || srcSize == 0)
    {
        return;
    }

    // Copy characters from src to dst until either (dstSize - 1) is exhausted or we hit the end of src.
    while (dstSize > 1 && srcSize > 0)
    {
        *dst++ = *src++;
        --dstSize;
        --srcSize;
    }
    // Fill the rest of dst with null characters to ensure null-termination.
    while (dstSize > 0)
    {
        *dst++ = 0;
        --dstSize;
    }
}

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
