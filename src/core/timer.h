#pragma once

#include <cstdint>

namespace rhi {

// Time point in nanoseconds.
using TimePoint = uint64_t;

/// High resolution CPU timer.
class Timer
{
public:
    Timer() { reset(); }

    /// Reset the timer.
    void reset() { m_start = now(); }

    /// Elapsed seconds since last reset.
    double elapsed() const { return delta(m_start, now()); }

    /// Elapsed milliseconds since last reset.
    double elapsedMS() const { return deltaMS(m_start, now()); }

    /// Elapsed microseconds since last reset.
    double elapsedUS() const { return deltaUS(m_start, now()); }

    /// Elapsed nanoseconds since last reset.
    double elapsedNS() const { return deltaNS(m_start, now()); }

    /// Compute elapsed seconds between two time points.
    static double delta(TimePoint start, TimePoint end) { return (end - start) * 1e-9; }

    /// Compute elapsed milliseconds between two time points.
    static double deltaMS(TimePoint start, TimePoint end) { return (end - start) * 1e-6; }

    /// Compute elapsed microseconds between two time points.
    static double deltaUS(TimePoint start, TimePoint end) { return (end - start) * 1e-3; }

    /// Compute elapsed nanoseconds between two time points.
    static double deltaNS(TimePoint start, TimePoint end) { return double(end - start); }

    /// Current time point in nanoseconds since epoch.
    static TimePoint now();

private:
    TimePoint m_start;
};

} // namespace rhi
