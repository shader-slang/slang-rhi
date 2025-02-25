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
}
