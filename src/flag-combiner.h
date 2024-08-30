#pragma once

#include <vector>
#include <cstdint>

namespace rhi {

/* A default set of flags that can be used for checking devices */
typedef uint32_t DeviceCheckFlags;
struct DeviceCheckFlag
{
    enum Enum : DeviceCheckFlags
    {
        /// If set will use full feature level (on dx this is  D3D_FEATURE_LEVEL_11_1 else will try
        /// D3D_FEATURE_LEVEL_11_0)
        UseFullFeatureLevel = 0x1,
        /// If set will try a hardware device
        UseHardwareDevice = 0x2,
        /// If set will enable use of debug
        UseDebug = 0x4,
    };
};

/* Controls how and the order flags are changed, on the FlagCombiner */
enum class ChangeType
{
    /// Always on.
    On,
    /// Always off.
    Off,
    /// Initially on then off.
    OnOff,
    /// Initially off then on.
    OffOn,
};

/* Calculates all the combinations of flags as controlled by the change types.

The order of adding flags can be considered to be like a nested loop
for (first added) {
    for (second added)
    {
        ///..
    }
}

So the last added flags will have the highest frequency.
*/
class FlagCombiner
{
public:
    /// Add a flag and how it changes over the combinations
    /// NOTE! That the order flags are added controls the order they change when combinations are calculated - earlier
    /// added flags will change with the highest frequency
    void add(uint32_t flags, ChangeType changeType);
    /// Calculate all of the combinations and place in an array
    void calcCombinations(std::vector<uint32_t>& outCombinations) const;

    /// Reset back to initial state
    void reset();

    /// Get the total amount of combinations
    int getNumCombinations() const { return 1 << m_numChangingBits; }

    /// Get the combination at i
    uint32_t getCombination(int i) const;

protected:
    uint32_t m_changingBits[32];
    int m_numChangingBits = 0;

    uint32_t m_usedFlags = 0;
    uint32_t m_invertBits = 0;
};

} // namespace rhi
