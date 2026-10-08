#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rhi::particles {

// Every backend receives the same absolute application time. Never use draw
// duration as simulation time; clamp catch-up after a stall to keep input usable.
class SimulationClock
{
public:
    static constexpr double kStep = 1.0 / 120.0;
    uint32_t update(double time, bool running)
    {
        double elapsed = m_lastTime < 0 ? 0 : std::clamp(time - m_lastTime, 0.0, 8 * kStep);
        m_lastTime = time;
        if (!running)
        {
            m_accumulator = 0;
            return 0;
        }
        m_accumulator += elapsed;
        uint32_t steps = uint32_t(std::floor((m_accumulator + 1e-10) / kStep));
        m_accumulator = std::max(0.0, m_accumulator - steps * kStep);
        return steps;
    }
    void reset() { m_accumulator = 0; }

private:
    double m_lastTime = -1, m_accumulator = 0;
};

} // namespace rhi::particles
