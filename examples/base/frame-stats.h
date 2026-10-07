#pragma once

#include <array>
#include <cstddef>

namespace rhi {

// Application-loop timing, shared in meaning across the side-by-side windows.
// A rolling window of 120 intervals smooths the display; publish at most 4 Hz.
class FrameStats
{
public:
    bool update(double time)
    {
        if (m_previousTime < 0 || time <= m_previousTime)
        {
            reset();
            m_previousTime = m_lastReport = time;
            return false;
        }
        double elapsed = time - m_previousTime;
        m_previousTime = time;
        m_total += elapsed - m_intervals[m_next];
        m_intervals[m_next] = elapsed;
        m_next = (m_next + 1) % m_intervals.size();
        if (m_count < m_intervals.size())
            ++m_count;
        if (time - m_lastReport < 0.25)
            return false;
        m_lastReport = time;
        m_frameTime = m_total / double(m_count);
        return true;
    }

    void reset() { *this = FrameStats(); }
    double fps() const { return m_frameTime > 0 ? 1.0 / m_frameTime : 0; }
    double milliseconds() const { return m_frameTime * 1000.0; }

private:
    std::array<double, 120> m_intervals = {};
    size_t m_next = 0, m_count = 0;
    double m_previousTime = -1, m_lastReport = 0, m_total = 0, m_frameTime = 0;
};

} // namespace rhi
