// Based on https://github.com/sebbbi/OffsetAllocator

// (C) Sebastian Aaltonen 2023
// MIT License

#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

#include "core/offset-allocator.h"

namespace rhi {
namespace SmallFloat {
extern uint32_t uintToFloatRoundUp(uint32_t size);
extern uint32_t uintToFloatRoundDown(uint32_t size);
extern uint32_t floatToUint(uint32_t floatValue);
} // namespace SmallFloat
} // namespace rhi

TEST_CASE("offset-allocator-small-float")
{
    SUBCASE("uintToFloat")
    {
        // Denorms, exp=1 and exp=2 + mantissa = 0 are all precise.
        // NOTE: Assuming 8 value (3 bit) mantissa.
        // If this test fails, please change this assumption!
        uint32_t preciseNumberCount = 17;
        for (uint32_t i = 0; i < preciseNumberCount; i++)
        {
            uint32_t roundUp = SmallFloat::uintToFloatRoundUp(i);
            uint32_t roundDown = SmallFloat::uintToFloatRoundDown(i);
            REQUIRE(i == roundUp);
            REQUIRE(i == roundDown);
        }

        // Test some random picked numbers
        struct NumberFloatUpDown
        {
            uint32_t number;
            uint32_t up;
            uint32_t down;
        };

        NumberFloatUpDown testData[] = {
            {17, 17, 16},
            {118, 39, 38},
            {1024, 64, 64},
            {65536, 112, 112},
            {529445, 137, 136},
            {1048575, 144, 143},
        };

        for (uint32_t i = 0; i < sizeof(testData) / sizeof(NumberFloatUpDown); i++)
        {
            NumberFloatUpDown v = testData[i];
            uint32_t roundUp = SmallFloat::uintToFloatRoundUp(v.number);
            uint32_t roundDown = SmallFloat::uintToFloatRoundDown(v.number);
            REQUIRE(roundUp == v.up);
            REQUIRE(roundDown == v.down);
        }
    }

    SUBCASE("floatToUint")
    {
        // Denorms, exp=1 and exp=2 + mantissa = 0 are all precise.
        // NOTE: Assuming 8 value (3 bit) mantissa.
        // If this test fails, please change this assumption!
        uint32_t preciseNumberCount = 17;
        for (uint32_t i = 0; i < preciseNumberCount; i++)
        {
            uint32_t v = SmallFloat::floatToUint(i);
            REQUIRE(i == v);
        }

        // Test that float->uint->float conversion is precise for all numbers
        // NOTE: Test values < 240. 240->4G = overflows 32 bit integer
        for (uint32_t i = 0; i < 240; i++)
        {
            uint32_t v = SmallFloat::floatToUint(i);
            uint32_t roundUp = SmallFloat::uintToFloatRoundUp(v);
            uint32_t roundDown = SmallFloat::uintToFloatRoundDown(v);
            REQUIRE(i == roundUp);
            REQUIRE(i == roundDown);
            // if ((i%8) == 0) printf("\n");
            // printf("%u->%u ", i, v);
        }
    }
}

TEST_CASE("offset-allocator-basic")
{
    OffsetAllocator allocator(1024 * 1024 * 256);
    OffsetAllocator::Allocation a = allocator.allocate(1337);
    uint32_t offset = a.offset;
    REQUIRE(offset == 0);
    allocator.free(a);
}

TEST_CASE("offset-allocator-alloc")
{
    OffsetAllocator allocator(1024 * 1024 * 256);

    SUBCASE("simple")
    {
        // Free merges neighbor empty nodes. Next allocation should also have offset = 0
        OffsetAllocator::Allocation a = allocator.allocate(0);
        REQUIRE(a.offset == 0);

        OffsetAllocator::Allocation b = allocator.allocate(1);
        REQUIRE(b.offset == 0);

        OffsetAllocator::Allocation c = allocator.allocate(123);
        REQUIRE(c.offset == 1);

        OffsetAllocator::Allocation d = allocator.allocate(1234);
        REQUIRE(d.offset == 124);

        allocator.free(a);
        allocator.free(b);
        allocator.free(c);
        allocator.free(d);

        // End: Validate that allocator has no fragmentation left. Should be 100% clean.
        OffsetAllocator::Allocation validateAll = allocator.allocate(1024 * 1024 * 256);
        REQUIRE(validateAll.offset == 0);
        allocator.free(validateAll);
    }

    SUBCASE("merge trivial")
    {
        // Free merges neighbor empty nodes. Next allocation should also have offset = 0
        OffsetAllocator::Allocation a = allocator.allocate(1337);
        REQUIRE(a.offset == 0);
        allocator.free(a);

        OffsetAllocator::Allocation b = allocator.allocate(1337);
        REQUIRE(b.offset == 0);
        allocator.free(b);

        // End: Validate that allocator has no fragmentation left. Should be 100% clean.
        OffsetAllocator::Allocation validateAll = allocator.allocate(1024 * 1024 * 256);
        REQUIRE(validateAll.offset == 0);
        allocator.free(validateAll);
    }

    SUBCASE("reuse trivial")
    {
        // Allocator should reuse node freed by A since the allocation C fits in the same bin (using pow2 size to be
        // sure)
        OffsetAllocator::Allocation a = allocator.allocate(1024);
        REQUIRE(a.offset == 0);

        OffsetAllocator::Allocation b = allocator.allocate(3456);
        REQUIRE(b.offset == 1024);

        allocator.free(a);

        OffsetAllocator::Allocation c = allocator.allocate(1024);
        REQUIRE(c.offset == 0);

        allocator.free(c);
        allocator.free(b);

        // End: Validate that allocator has no fragmentation left. Should be 100% clean.
        OffsetAllocator::Allocation validateAll = allocator.allocate(1024 * 1024 * 256);
        REQUIRE(validateAll.offset == 0);
        allocator.free(validateAll);
    }

    SUBCASE("reuse complex")
    {
        // Allocator should not reuse node freed by A since the allocation C doesn't fits in the same bin
        // However node D and E fit there and should reuse node from A
        OffsetAllocator::Allocation a = allocator.allocate(1024);
        REQUIRE(a.offset == 0);

        OffsetAllocator::Allocation b = allocator.allocate(3456);
        REQUIRE(b.offset == 1024);

        allocator.free(a);

        OffsetAllocator::Allocation c = allocator.allocate(2345);
        REQUIRE(c.offset == 1024 + 3456);

        OffsetAllocator::Allocation d = allocator.allocate(456);
        REQUIRE(d.offset == 0);

        OffsetAllocator::Allocation e = allocator.allocate(512);
        REQUIRE(e.offset == 456);

        OffsetAllocator::StorageReport report = allocator.storageReport();
        REQUIRE(report.totalFreeSpace == 1024 * 1024 * 256 - 3456 - 2345 - 456 - 512);
        REQUIRE(report.largestFreeRegion != report.totalFreeSpace);

        allocator.free(c);
        allocator.free(d);
        allocator.free(b);
        allocator.free(e);

        // End: Validate that allocator has no fragmentation left. Should be 100% clean.
        OffsetAllocator::Allocation validateAll = allocator.allocate(1024 * 1024 * 256);
        REQUIRE(validateAll.offset == 0);
        allocator.free(validateAll);
    }

    SUBCASE("zero fragmentation")
    {
        // Allocate 256x 1MB. Should fit. Then free four random slots and reallocate four slots.
        // Plus free four contiguous slots an allocate 4x larger slot. All must be zero fragmentation!
        OffsetAllocator::Allocation allocations[256];
        for (uint32_t i = 0; i < 256; i++)
        {
            allocations[i] = allocator.allocate(1024 * 1024);
            REQUIRE(allocations[i].offset == i * 1024 * 1024);
        }

        OffsetAllocator::StorageReport report = allocator.storageReport();
        REQUIRE(report.totalFreeSpace == 0);
        REQUIRE(report.largestFreeRegion == 0);

        // Free four random slots
        allocator.free(allocations[243]);
        allocator.free(allocations[5]);
        allocator.free(allocations[123]);
        allocator.free(allocations[95]);

        // Free four contiguous slot (allocator must merge)
        allocator.free(allocations[151]);
        allocator.free(allocations[152]);
        allocator.free(allocations[153]);
        allocator.free(allocations[154]);

        allocations[243] = allocator.allocate(1024 * 1024);
        allocations[5] = allocator.allocate(1024 * 1024);
        allocations[123] = allocator.allocate(1024 * 1024);
        allocations[95] = allocator.allocate(1024 * 1024);
        allocations[151] = allocator.allocate(1024 * 1024 * 4); // 4x larger
        REQUIRE(allocations[243].offset != OffsetAllocator::Allocation::NO_SPACE);
        REQUIRE(allocations[5].offset != OffsetAllocator::Allocation::NO_SPACE);
        REQUIRE(allocations[123].offset != OffsetAllocator::Allocation::NO_SPACE);
        REQUIRE(allocations[95].offset != OffsetAllocator::Allocation::NO_SPACE);
        REQUIRE(allocations[151].offset != OffsetAllocator::Allocation::NO_SPACE);

        for (uint32_t i = 0; i < 256; i++)
        {
            if (i < 152 || i > 154)
                allocator.free(allocations[i]);
        }

        OffsetAllocator::StorageReport report2 = allocator.storageReport();
        REQUIRE(report2.totalFreeSpace == 1024 * 1024 * 256);
        REQUIRE(report2.largestFreeRegion == 1024 * 1024 * 256);

        // End: Validate that allocator has no fragmentation left. Should be 100% clean.
        OffsetAllocator::Allocation validateAll = allocator.allocate(1024 * 1024 * 256);
        REQUIRE(validateAll.offset == 0);
        allocator.free(validateAll);
    }
}
