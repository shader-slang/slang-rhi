#pragma once

#include <string>
#include <string_view>

namespace gfx {

    inline std::wstring to_wstring(std::string_view str) {
        std::wstring wstr;
        wstr.assign(str.begin(), str.end());
        return wstr;
    }

    inline std::string from_wstring(std::wstring_view wstr) {
        std::string str(wstr.size(), '\0');
        for (size_t i = 0; i < wstr.size(); i++) {
            str[i] = static_cast<char>(wstr[i]);
        }
        return str;
    }

} // namespace gfx