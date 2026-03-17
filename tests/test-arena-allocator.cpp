#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

#include "core/arena-allocator.h"

TEST_CASE("arena-allocator")
{
    SUBCASE("allocate")
    {
        ArenaAllocator allocator(1024);

        static constexpr size_t kIterations = 100;
        static const size_t kSizes[] = {1, 2, 3, 7, 17, 30, 62, 120, 260, 522, 1014, 2013, 4099, 8213};
        static const size_t kAligments[] = {1, 1, 2, 2, 4, 4, 8, 8, 16, 16, 32, 32, 64, 128};

        std::vector<std::pair<uintptr_t, uintptr_t>> allocations;

        for (size_t i = 0; i < kIterations; i++)
        {
            for (size_t j = 0; j < sizeof(kSizes) / sizeof(size_t); j++)
            {
                size_t size = kSizes[j];
                size_t alignment = kAligments[j];
                void* a = allocator.allocate(size, alignment);
                CHECK(a != nullptr);
                uintptr_t begin = (uintptr_t)a;
                uintptr_t end = begin + size;
                CHECK(begin % alignment == 0);
                for (size_t k = 0; k < allocations.size(); k++)
                {
                    CHECK_UNARY((end <= allocations[k].first) || (begin >= allocations[k].second));
                }
                allocations.push_back({(uintptr_t)a, (uintptr_t)a + size});
            }
        }
    }

    SUBCASE("first-allocation-larger-than-page")
    {
        // The first page is allocated with exactly m_pageSize bytes (including the Page header).
        // If the first allocation requires more usable space than m_pageSize - sizeof(Page header),
        // it will write past the end of the page.
        // Use a small page size so the allocation exceeds it.
        ArenaAllocator allocator(64);

        // Allocate more than the page size. This should still succeed and not corrupt memory.
        size_t largeSize = 256;
        void* a = allocator.allocate(largeSize, 16);
        CHECK(a != nullptr);
        CHECK(((uintptr_t)a % 16) == 0);

        // Write to the entire allocation to detect out-of-bounds issues (e.g. with ASan).
        std::memset(a, 0xAB, largeSize);

        // A subsequent small allocation should not overlap.
        void* b = allocator.allocate(32, 16);
        CHECK(b != nullptr);
        CHECK(((uintptr_t)b % 16) == 0);

        uintptr_t aBegin = (uintptr_t)a;
        uintptr_t aEnd = aBegin + largeSize;
        uintptr_t bBegin = (uintptr_t)b;
        uintptr_t bEnd = bBegin + 32;
        CHECK_UNARY((bBegin >= aEnd) || (bEnd <= aBegin));
    }

    SUBCASE("reset")
    {
        ArenaAllocator allocator(1024);

        std::vector<void*> allocations;

        for (size_t i = 0; i < 100; i++)
        {
            void* a = allocator.allocate(100);
            CHECK(a != nullptr);
            allocations.push_back(a);
        }

        allocator.reset();

        for (size_t i = 0; i < 100; i++)
        {
            void* a = allocator.allocate(100);
            CHECK(a == allocations[i]);
        }
    }

    SUBCASE("reset-with-larger-allocation")
    {
        // After reset, reused pages may be too small for a larger allocation.
        // The allocator must detect this and allocate a new page.
        ArenaAllocator allocator(128);

        // First pass: small allocations that fit in small pages.
        for (size_t i = 0; i < 10; i++)
        {
            void* a = allocator.allocate(16, 16);
            CHECK(a != nullptr);
            std::memset(a, 0xAB, 16);
        }

        allocator.reset();

        // Second pass: request a large allocation that exceeds the reused page size.
        size_t largeSize = 512;
        void* a = allocator.allocate(largeSize, 16);
        CHECK(a != nullptr);
        CHECK(((uintptr_t)a % 16) == 0);
        std::memset(a, 0xCD, largeSize);

        // A subsequent allocation should not overlap.
        void* b = allocator.allocate(32, 16);
        CHECK(b != nullptr);
        CHECK(((uintptr_t)b % 16) == 0);

        uintptr_t aBegin = (uintptr_t)a;
        uintptr_t aEnd = aBegin + largeSize;
        uintptr_t bBegin = (uintptr_t)b;
        uintptr_t bEnd = bBegin + 32;
        CHECK_UNARY((bBegin >= aEnd) || (bEnd <= aBegin));
    }
}
