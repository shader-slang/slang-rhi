#pragma once

#include <sstream>
#include <string>
#include <vector>

namespace rhi::testing::stress {

class OperationLog
{
public:
    explicit OperationLog(size_t capacity = 128) { reset(capacity); }

    void reset(size_t capacity)
    {
        m_capacity = capacity;
        m_entries.clear();
        m_next = 0;
    }

    void add(std::string entry)
    {
        if (m_capacity == 0)
            return;
        if (m_entries.size() < m_capacity)
        {
            m_entries.push_back(std::move(entry));
            return;
        }
        m_entries[m_next] = std::move(entry);
        m_next = (m_next + 1) % m_capacity;
    }

    std::string toString() const
    {
        std::ostringstream stream;
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            size_t index = m_entries.size() == m_capacity ? (m_next + i) % m_entries.size() : i;
            stream << "  " << m_entries[index] << "\n";
        }
        return stream.str();
    }

private:
    size_t m_capacity = 0;
    size_t m_next = 0;
    std::vector<std::string> m_entries;
};

} // namespace rhi::testing::stress
