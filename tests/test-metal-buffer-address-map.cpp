#include "testing.h"

#if SLANG_RHI_ENABLE_METAL
#include "metal/metal-buffer-address-map.h"

#include <array>
#include <limits>
#include <memory>
#include <random>

using namespace rhi;

TEST_CASE("resource-heap-metal-buffer-address-map")
{
    // No native allocations are needed to exercise the residency fallback index.
    metal::BufferImpl small(nullptr, {});
    metal::BufferImpl large(nullptr, {});
    metal::BufferImpl duplicate(nullptr, {});
    metal::BufferImpl nested(nullptr, {});
    metal::BufferImpl extending(nullptr, {});
    metal::BufferAddressMap map;

    for (bool largestFirst : {false, true})
    {
        CAPTURE(largestFirst);
        if (largestFirst)
        {
            map.insert(100, 100, &large);
            map.insert(100, 20, &small);
        }
        else
        {
            map.insert(100, 20, &small);
            map.insert(100, 100, &large);
        }
        map.insert(100, 20, &duplicate);
        map.insert(130, 10, &nested);
        map.insert(180, 40, &extending);

        CHECK(map.find(99) == nullptr);
        CHECK(map.find(100) == &large);
        CHECK(map.find(119) == &large);
        CHECK(map.find(120) == &large);
        CHECK(map.find(130) == &nested); // Exact matches retain their fast path.
        CHECK(map.find(150) == &large);  // The preceding nested range has already ended.
        CHECK(map.find(199) == &large);
        CHECK(map.find(200) == &extending);
        CHECK(map.find(219) == &extending);
        CHECK(map.find(220) == nullptr);

        // Removing a non-head alias must preserve the larger range.
        map.erase(100, &duplicate);
        CHECK(duplicate.m_nextAtSameAddr == nullptr);
        CHECK(map.find(150) == &large);

        // Removing the largest alias shrinks coverage and rebuilds the range index.
        map.erase(100, &large);
        CHECK(large.m_nextAtSameAddr == nullptr);
        CHECK(map.find(100) == &small);
        CHECK(map.find(119) == &small);
        CHECK(map.find(120) == nullptr);
        CHECK(map.find(129) == nullptr);
        CHECK(map.find(139) == &nested);
        CHECK(map.find(140) == nullptr);
        CHECK(map.find(179) == nullptr);
        CHECK(map.find(180) == &extending);
        CHECK(map.find(200) == &extending);

        // Reinsert the larger alias after a range lookup has built the cache.
        map.insert(100, 100, &large);
        CHECK(map.find(150) == &large);
        map.erase(100, &small);
        CHECK(map.find(100) == &large);
        map.erase(100, &large);
        CHECK(map.find(100) == nullptr);
        CHECK(map.find(150) == nullptr);
        map.erase(130, &nested);
        map.erase(180, &extending);
        CHECK(map.find(200) == nullptr);
    }
}

TEST_CASE("resource-heap-metal-buffer-address-map-unordered-aliases")
{
    metal::BufferImpl largest(nullptr, {});
    metal::BufferImpl medium(nullptr, {});
    metal::BufferImpl small(nullptr, {});
    metal::BufferImpl duplicate(nullptr, {});
    metal::BufferAddressMap map;

    map.insert(100, 100, &largest);
    map.insert(100, 60, &medium);
    map.insert(100, 20, &small);
    CHECK(map.find(199) == &largest); // Populate the range index before changing aliases.
    map.insert(100, 100, &duplicate);
    CHECK(map.find(199) == &largest);
    map.erase(100, &largest);
    CHECK(map.find(100) == &duplicate);
    CHECK(map.find(199) == &duplicate);

    // The next largest alias is behind a smaller alias in the unordered chain.
    map.erase(100, &duplicate);
    CHECK(map.find(100) == &medium);
    CHECK(map.find(159) == &medium);
    CHECK(map.find(160) == nullptr);
    CHECK(largest.m_nextAtSameAddr == nullptr);
    CHECK(duplicate.m_nextAtSameAddr == nullptr);

    // An absent buffer or base address must leave the index unchanged.
    map.erase(100, &largest);
    map.erase(99, &medium);
    CHECK(map.find(159) == &medium);
    map.erase(100, &small);
    CHECK(map.find(159) == &medium);
    map.erase(100, &medium);
    CHECK(map.find(100) == nullptr);
    CHECK(map.find(159) == nullptr);
}

TEST_CASE("resource-heap-metal-buffer-address-map-address-boundaries")
{
    metal::BufferImpl low(nullptr, {});
    metal::BufferImpl high(nullptr, {});
    metal::BufferAddressMap map;
    const DeviceAddress last = std::numeric_limits<DeviceAddress>::max();

    CHECK(map.find(0) == nullptr);
    CHECK(map.find(last) == nullptr);
    map.insert(0, 1, &low);
    map.insert(last - 15, 16, &high);
    CHECK(map.find(0) == &low);
    CHECK(map.find(1) == nullptr);
    CHECK(map.find(last - 16) == nullptr);
    CHECK(map.find(last - 15) == &high);
    CHECK(map.find(last - 1) == &high);
    CHECK(map.find(last) == &high);
    map.erase(last - 15, &high);
    CHECK(map.find(last) == nullptr);
    map.erase(0, &low);
    CHECK(map.find(0) == nullptr);
}

TEST_CASE("resource-heap-metal-buffer-address-map-randomized")
{
    struct Registration
    {
        std::unique_ptr<metal::BufferImpl> buffer = std::make_unique<metal::BufferImpl>(nullptr, BufferDesc{});
        DeviceAddress base = 0;
        DeviceAddress size = 0;
        bool active = false;
    };
    std::array<Registration, 16> registrations;
    metal::BufferAddressMap map;
    std::mt19937 random(940);

    // Compare mixed alias insertion/removal and cache rebuilding against a linear
    // coverage oracle. Do not require a particular representative for equal sizes.
    for (uint32_t iteration = 0; iteration < 1000; ++iteration)
    {
        CAPTURE(iteration);
        auto& changed = registrations[random() % registrations.size()];
        if (changed.active)
        {
            map.erase(changed.base, changed.buffer.get());
            CHECK(changed.buffer->m_nextAtSameAddr == nullptr);
            CHECK(changed.buffer->m_addressMapSize == 0);
        }
        else
        {
            changed.base = (random() % 8) * 8;
            changed.size = 1 + random() % 64;
            map.insert(changed.base, changed.size, changed.buffer.get());
        }
        changed.active = !changed.active;

        for (DeviceAddress addr = 0; addr < 128; ++addr)
        {
            CAPTURE(addr);
            bool covered = false;
            DeviceAddress largestAtBase = 0;
            for (const auto& entry : registrations)
            {
                if (!entry.active)
                    continue;
                covered |= addr >= entry.base && addr - entry.base < entry.size;
                if (addr == entry.base)
                    largestAtBase = std::max(largestAtBase, entry.size);
            }

            auto* found = map.find(addr);
            REQUIRE((found != nullptr) == covered);
            if (found)
            {
                bool valid = false;
                for (const auto& entry : registrations)
                {
                    if (entry.buffer.get() != found)
                        continue;
                    valid = entry.active && addr >= entry.base && addr - entry.base < entry.size;
                    if (largestAtBase)
                        valid &= addr == entry.base && entry.size == largestAtBase;
                    break;
                }
                CHECK(valid);
            }
        }
    }
    for (auto& entry : registrations)
    {
        if (entry.active)
            map.erase(entry.base, entry.buffer.get());
    }
    CHECK(map.find(64) == nullptr);
}
#endif
