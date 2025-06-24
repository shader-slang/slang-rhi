#pragma once

#include <string>
#include <string_view>

namespace rhi {

class StringBuilder
{
public:
    StringBuilder() = default;

    void clear() { m_str.clear(); }

    const std::string& getString() const { return m_str; }

    StringBuilder& append(const char* str)
    {
        if (str)
        {
            m_str.append(str);
        }
        return *this;
    }

    StringBuilder& append(const std::string& str)
    {
        m_str.append(str);
        return *this;
    }

    StringBuilder& append(std::string_view str)
    {
        m_str.append(str);
        return *this;
    }

    StringBuilder& append(char c)
    {
        m_str.push_back(c);
        return *this;
    }

    StringBuilder& indent(size_t count)
    {
        m_str.append(count, ' ');
        return *this;
    }

    // stream-like operator overloads

    StringBuilder& operator<<(const char* str) { return append(str); }

    StringBuilder& operator<<(const std::string& str) { return append(str); }

    StringBuilder& operator<<(std::string_view str) { return append(str); }

    StringBuilder& operator<<(char c) { return append(c); }

private:
    std::string m_str;
};

} // namespace rhi
