#include "testing.h"

#if SLANG_RHI_ENABLE_METAL
#include "metal/metal-buffer-address-map.h"

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
#endif
