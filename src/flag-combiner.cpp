#include "flag-combiner.h"

#include "core/common.h"

namespace rhi {

void FlagCombiner::add(uint32_t flags, ChangeType type)
{
    // The flag/s must be set
    SLANG_RHI_ASSERT(flags);
    SLANG_RHI_ASSERT((flags & m_usedFlags) == 0);
    // Mark the flags used
    m_usedFlags |= flags;

    if (type == ChangeType::On || type == ChangeType::OnOff)
    {
        m_invertBits |= flags;
    }
    if (type == ChangeType::OnOff || type == ChangeType::OffOn)
    {
        m_changingBits[m_numChangingBits++] = flags;
    }
}

void FlagCombiner::calcCombinations(std::vector<uint32_t>& outCombinations) const
{
    const int numCombinations = getNumCombinations();
    outCombinations.resize(numCombinations);
    uint32_t* dstCombinations = outCombinations.data();
    for (int i = 0; i < numCombinations; ++i)
    {
        dstCombinations[i] = getCombination(i);
    }
}

uint32_t FlagCombiner::getCombination(int index) const
{
    SLANG_RHI_ASSERT(index >= 0 && index < getNumCombinations());

    uint32_t combination = 0;
    uint32_t bit = 1;
    for (int i = m_numChangingBits - 1; i >= 0; --i, bit += bit)
    {
        combination |= ((bit & index) ? m_changingBits[i] : 0);
    }
    return combination ^ m_invertBits;
}

void FlagCombiner::reset()
{
    m_numChangingBits = 0;
    m_usedFlags = 0;
    m_invertBits = 0;
}

} // namespace rhi
