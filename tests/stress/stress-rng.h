#pragma once

#include <cstdint>
#include <string_view>

namespace rhi::testing::stress {

inline uint64_t mix64(uint64_t value)
{
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebull;
    value ^= value >> 31;
    return value;
}

inline uint64_t hashString(std::string_view value)
{
    uint64_t hash = 14695981039346656037ull;
    for (char c : value)
    {
        hash ^= uint8_t(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

class Rng
{
public:
    explicit Rng(uint64_t seed = 0) { reset(seed); }

    void reset(uint64_t seed)
    {
        m_state = 0;
        m_inc = (mix64(seed ^ 0xda3e39cb94b95bdbull) << 1) | 1;
        nextU32();
        m_state += mix64(seed);
        nextU32();
    }

    uint32_t nextU32()
    {
        uint64_t oldState = m_state;
        m_state = oldState * 6364136223846793005ull + m_inc;
        uint32_t xorshifted = uint32_t(((oldState >> 18u) ^ oldState) >> 27u);
        uint32_t rot = uint32_t(oldState >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31));
    }

    uint32_t nextRange(uint32_t limit)
    {
        if (limit == 0)
            return 0;
        uint32_t threshold = (0u - limit) % limit;
        for (;;)
        {
            uint32_t value = nextU32();
            if (value >= threshold)
                return value % limit;
        }
    }

    bool chance(uint32_t numerator, uint32_t denominator)
    {
        return denominator != 0 && nextRange(denominator) < numerator;
    }

private:
    uint64_t m_state = 0;
    uint64_t m_inc = 1;
};

} // namespace rhi::testing::stress
