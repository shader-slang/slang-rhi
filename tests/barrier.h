#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace rhi::testing {

// Keep test synchronization visible to TSAN. libc++'s std::barrier performs
// part of its synchronization in the uninstrumented system library on macOS.
class Barrier
{
public:
    explicit Barrier(uint32_t count)
        : m_count(count)
        , m_remaining(count)
    {
    }

    void arriveAndWait()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        const uint64_t generation = m_generation;
        if (--m_remaining == 0)
        {
            m_remaining = m_count;
            ++m_generation;
            m_condition.notify_all();
        }
        else
        {
            m_condition.wait(
                lock,
                [&]
                {
                    return m_generation != generation;
                }
            );
        }
    }

private:
    const uint32_t m_count;
    uint32_t m_remaining;
    uint64_t m_generation = 0;
    std::mutex m_mutex;
    std::condition_variable m_condition;
};

} // namespace rhi::testing
