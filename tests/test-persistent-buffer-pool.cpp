#include "testing.h"
#include "../src/device.h"
#include "../src/persistent-buffer-pool.h"

#include <algorithm>
#include <atomic>
#include <barrier>
#include <cstring>
#include <limits>
#include <thread>

using namespace rhi;
using namespace rhi::testing;

namespace {
struct TestPool : PersistentBufferPool
{
    ~TestPool() { release(); }
};
} // namespace

GPU_TEST_CASE("persistent-buffer-pool-reuse", D3D12 | Vulkan | Metal | DontCacheDevice)
{
    auto native = getUnderlyingDevice(device.get());
    const auto deviceRefs = native->getReferenceCount();
    TestPool pool;
    pool.initialize(native, 256, 1024, 1024);
    RefPtr<PersistentBufferPool::Allocation> a, b, c;
    REQUIRE_CALL(pool.allocate(1, a));
    REQUIRE_CALL(pool.allocate(33, b));
    CHECK(native->getReferenceCount() == deviceRefs + 2);
    CHECK(a->getBuffer() == b->getBuffer());
    CHECK(a->getOffset() != b->getOffset());
    CHECK(a->getOffset() % 256 == 0);
    CHECK(b->getOffset() % 256 == 0);
    CHECK(a->getSize() == 256);
    std::memset(a->getMappedData(), 0xa5, a->getSize());
    std::memset(b->getMappedData(), 0x5a, b->getSize());
    auto offset = a->getOffset();
    a = nullptr;
    REQUIRE_CALL(pool.allocate(100, c));
    CHECK(c->getBuffer() == b->getBuffer());
    CHECK(c->getOffset() == offset);
    std::memset(c->getMappedData(), 0xff, c->getSize());
    auto bytes = static_cast<uint8_t*>(b->getMappedData());
    CHECK(
        std::all_of(
            bytes,
            bytes + b->getSize(),
            [](uint8_t v)
            {
                return v == 0x5a;
            }
        )
    );
    CHECK(pool.getStats().pageAllocationCount == 1);
    b = nullptr;
    c = nullptr;
    CHECK(pool.getStats().used == 0);
    CHECK(pool.getStats().allocationCount == 0);
    CHECK(pool.getStats().capacity == 1024);
    CHECK(native->getReferenceCount() == deviceRefs);
    REQUIRE_CALL(pool.allocate(1024, a));
    CHECK(pool.getStats().pageAllocationCount == 1);
    a = nullptr;
    // Every aligned slot must remain usable, including the allocator's final metadata node.
    std::vector<RefPtr<PersistentBufferPool::Allocation>> slots(4);
    for (auto& slot : slots)
        REQUIRE_CALL(pool.allocate(1, slot));
    CHECK(pool.getStats().pageAllocationCount == 1);
    CHECK(pool.getStats().used == 1024);
    slots.clear();
    CHECK(pool.allocate(0, a) == SLANG_E_INVALID_ARG);
    CHECK(pool.allocate(std::numeric_limits<Size>::max(), a) == SLANG_E_INVALID_ARG);
    CHECK(!a);
    CHECK(pool.getStats().allocationCount == 0);
}

GPU_TEST_CASE("persistent-buffer-pool-retention", D3D12 | Vulkan | Metal | DontCacheDevice)
{
    TestPool pool;
    pool.initialize(getUnderlyingDevice(device.get()), 256, 1024, 1024);
    std::vector<RefPtr<PersistentBufferPool::Allocation>> allocations(20);
    for (auto& allocation : allocations)
        REQUIRE_CALL(pool.allocate(257, allocation));
    CHECK(pool.getStats().capacity == 10 * 1024);
    CHECK(pool.getStats().used == 10 * 1024);
    // Release in an order that leaves holes in live pages, then coalesces all ranges.
    for (size_t i = 0; i < allocations.size(); i += 2)
        allocations[i] = nullptr;
    CHECK(pool.getStats().capacity == 10 * 1024);
    for (size_t i = 0; i < allocations.size(); i += 2)
        REQUIRE_CALL(pool.allocate(257, allocations[i]));
    CHECK(pool.getStats().pageAllocationCount == 10);
    allocations.clear();
    CHECK(pool.getStats().capacity == 1024);
    RefPtr<PersistentBufferPool::Allocation> large;
    REQUIRE_CALL(pool.allocate(4096, large));
    CHECK(large->getSize() == 4096);
    CHECK(pool.getStats().capacity == 5 * 1024);
    large = nullptr;
    CHECK(pool.getStats().capacity <= 1024);
    CHECK(pool.getStats().used == 0);
    pool.release();
    CHECK(pool.getStats().capacity == 0);
    CHECK(pool.getStats().pageCount == 0);
}

GPU_TEST_CASE("persistent-buffer-pool-parallel", D3D12 | Vulkan | Metal | DontCacheDevice)
{
    TestPool pool;
    pool.initialize(getUnderlyingDevice(device.get()), 256, 4096, 4096);
    std::atomic<bool> valid{true};
    std::barrier ready(4);
    std::thread workers[4];
    for (uint32_t index = 0; index < 4; ++index)
    {
        workers[index] = std::thread(
            [&, index]
            {
                ready.arrive_and_wait();
                for (uint32_t i = 0; i < 128; ++i)
                {
                    std::vector<RefPtr<PersistentBufferPool::Allocation>> allocations(8);
                    for (auto& allocation : allocations)
                    {
                        if (SLANG_FAILED(pool.allocate(1 + (i * 37) % 600, allocation)))
                        {
                            valid = false;
                            return;
                        }
                        std::memset(allocation->getMappedData(), int(index + 1), allocation->getSize());
                    }
                    std::this_thread::yield();
                    for (auto& allocation : allocations)
                    {
                        auto bytes = static_cast<uint8_t*>(allocation->getMappedData());
                        if (!std::all_of(
                                bytes,
                                bytes + allocation->getSize(),
                                [&](uint8_t v)
                                {
                                    return v == index + 1;
                                }
                            ))
                            valid = false;
                    }
                }
            }
        );
    }
    for (auto& worker : workers)
        worker.join();
    CHECK(valid.load());
    CHECK(pool.getStats().used == 0);
    CHECK(pool.getStats().allocationCount == 0);
    CHECK(pool.getStats().capacity <= 4096);
}
