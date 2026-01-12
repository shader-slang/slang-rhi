#include "testing.h"
#include "../src/core/block-allocator.h"

#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <set>

using namespace rhi;

// Test struct for block allocator
struct TestObject
{
    int value;
    double data;
    std::thread::id thread;
    char padding[128]; // Make it bigger to test alignment

    TestObject()
        : value(0)
        , data(0.0)
        , thread()
    {
    }

    TestObject(int v, double d, std::thread::id t = {})
        : value(v)
        , data(d)
        , thread(t)
    {
    }
};

TEST_CASE("block-allocator-single-threaded")
{
    BlockAllocator<TestObject> allocator(4); // Small page size for testing

    SUBCASE("basic-allocation")
    {
        TestObject* obj = allocator.allocate();
        REQUIRE(obj != nullptr);

        // Check alignment
        REQUIRE(reinterpret_cast<uintptr_t>(obj) % alignof(TestObject) == 0);

        // Use placement new to construct
        new (obj) TestObject(42, 3.14);
        CHECK(obj->value == 42);
        CHECK(obj->data == 3.14);

        // Destruct and deallocate
        obj->~TestObject();
        allocator.free(obj);
    }

    SUBCASE("multiple-allocations")
    {
        std::vector<TestObject*> objects;

        // Allocate more than one page worth
        for (int i = 0; i < 10; ++i)
        {
            TestObject* obj = allocator.allocate();
            REQUIRE(obj != nullptr);
            new (obj) TestObject(i, i * 1.5);
            objects.push_back(obj);
        }

        // Verify values
        for (int i = 0; i < 10; ++i)
        {
            CHECK(objects[i]->value == i);
            CHECK(objects[i]->data == i * 1.5);
        }

        // Deallocate all
        for (auto obj : objects)
        {
            obj->~TestObject();
            allocator.free(obj);
        }
    }

    SUBCASE("reuse-after-free")
    {
        // Allocate and free
        TestObject* obj1 = allocator.allocate();
        REQUIRE(obj1 != nullptr);
        new (obj1) TestObject(1, 1.0);
        obj1->~TestObject();
        allocator.free(obj1);

        // Allocate again - should reuse the same block
        TestObject* obj2 = allocator.allocate();
        REQUIRE(obj2 != nullptr);
        REQUIRE(obj2 == obj1); // Same memory location

        new (obj2) TestObject(2, 2.0);
        CHECK(obj2->value == 2);
        obj2->~TestObject();
        allocator.free(obj2);
    }

    SUBCASE("allocate-multiple-pages")
    {
        std::vector<TestObject*> objects;

        // Allocate 3 pages worth (4 blocks per page)
        for (int i = 0; i < 12; ++i)
        {
            TestObject* obj = allocator.allocate();
            REQUIRE(obj != nullptr);
            objects.push_back(obj);
        }

        // All should be unique
        std::set<TestObject*> uniqueObjects(objects.begin(), objects.end());
        CHECK(uniqueObjects.size() == 12);

        // Deallocate all
        for (auto obj : objects)
        {
            allocator.free(obj);
        }
    }
}

TEST_CASE("block-allocator-ownership")
{
    BlockAllocator<TestObject> allocator(16);

    SUBCASE("owns-allocated-blocks")
    {
        TestObject* obj = allocator.allocate();
        REQUIRE(obj != nullptr);

        CHECK(allocator.owns(obj));

        allocator.free(obj);

        // Still owns the memory even after deallocation
        CHECK(allocator.owns(obj));
    }

    SUBCASE("does-not-own-heap-pointers")
    {
        TestObject* heapObj = new TestObject();
        CHECK_FALSE(allocator.owns(heapObj));
        delete heapObj;
    }

    SUBCASE("does-not-own-stack-pointers")
    {
        TestObject stackObj;
        CHECK_FALSE(allocator.owns(&stackObj));
    }

    SUBCASE("does-not-own-nullptr")
    {
        CHECK_FALSE(allocator.owns(nullptr));
    }

    SUBCASE("owns-all-blocks-in-page")
    {
        std::vector<TestObject*> objects;

        // Allocate a full page
        for (int i = 0; i < 16; ++i)
        {
            TestObject* obj = allocator.allocate();
            REQUIRE(obj != nullptr);
            objects.push_back(obj);
        }

        // Should own all of them
        for (auto obj : objects)
        {
            CHECK(allocator.owns(obj));
        }

        // Deallocate all
        for (auto obj : objects)
        {
            allocator.free(obj);
        }
    }
}

TEST_CASE("block-allocator-reset")
{
    BlockAllocator<TestObject> allocator(4);

    // Allocate some objects
    std::vector<TestObject*> objects;
    for (int i = 0; i < 8; ++i)
    {
        objects.push_back(allocator.allocate());
    }

    // Free half of them
    for (int i = 0; i < 4; ++i)
    {
        allocator.free(objects[i]);
    }

    // Reset the allocator
    allocator.reset();

    // After reset, should be able to allocate all blocks again
    // and get the same pointers back (in some order)
    std::set<TestObject*> originalPtrs(objects.begin(), objects.end());
    std::vector<TestObject*> newObjects;
    for (int i = 0; i < 8; ++i)
    {
        TestObject* obj = allocator.allocate();
        REQUIRE(obj != nullptr);
        newObjects.push_back(obj);
    }

    // All new allocations should be from the same set of pointers
    for (auto obj : newObjects)
    {
        CHECK(originalPtrs.count(obj) == 1);
    }

    // All should be unique
    std::set<TestObject*> newPtrs(newObjects.begin(), newObjects.end());
    CHECK(newPtrs.size() == 8);

    // Clean up
    for (auto obj : newObjects)
    {
        allocator.free(obj);
    }
}

TEST_CASE("block-allocator-multi-threaded")
{
    BlockAllocator<TestObject> allocator(64);

    constexpr int numThreads = 8;
    constexpr int allocationsPerThread = 10000;
    std::atomic<int> totalAllocations{0};
    std::atomic<int> totalDeallocations{0};

    auto threadFunc = [&]()
    {
        std::vector<TestObject*> localObjects;
        localObjects.reserve(allocationsPerThread);

        // Allocate
        for (int i = 0; i < allocationsPerThread; ++i)
        {
            TestObject* obj = allocator.allocate();
            if (obj)
            {
                new (obj) TestObject(i, i * 1.5);
                localObjects.push_back(obj);
                totalAllocations.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Verify
        for (size_t i = 0; i < localObjects.size(); ++i)
        {
            REQUIRE(localObjects[i]->value == static_cast<int>(i));
            REQUIRE(localObjects[i]->data == i * 1.5);
        }

        // Deallocate
        for (auto obj : localObjects)
        {
            obj->~TestObject();
            allocator.free(obj);
            totalDeallocations.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // Launch threads
    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back(threadFunc);
    }

    // Wait for all threads
    for (auto& thread : threads)
    {
        thread.join();
    }

    // Verify all allocations and deallocations completed
    CHECK(totalAllocations.load() == numThreads * allocationsPerThread);
    CHECK(totalDeallocations.load() == numThreads * allocationsPerThread);
}

TEST_CASE("block-allocator-stress-test")
{
    constexpr int blocksPerPage = 1000;
    BlockAllocator<TestObject> allocator(blocksPerPage);

    // Quick test for CI
    constexpr int numThreads = 16;
    constexpr int iterations = 20;
    constexpr int objectsPerIteration = 1000;

    // Mega test takes about 30 mins
    // constexpr int numThreads = 16;
    // constexpr int iterations = 10000;
    // constexpr int objectsPerIteration = 10000;

    auto threadFunc = [&]()
    {
        for (int iter = 0; iter < iterations; ++iter)
        {
            CHECK(allocator.getNumPages() < (numThreads * objectsPerIteration * 2) / blocksPerPage);

            std::vector<TestObject*> objects;
            objects.reserve(objectsPerIteration * 2);

            auto id = std::this_thread::get_id();

            // Allocate
            for (int i = 0; i < objectsPerIteration; ++i)
            {
                TestObject* obj = allocator.allocate();
                if (obj)
                {
                    new (obj) TestObject(i, i * 2.0, id);
                    objects.push_back(obj);
                }
            }

            // Deallocate half
            for (size_t i = 0; i < objects.size() / 2; ++i)
            {
                if (objects[i])
                {
                    CHECK_EQ(objects[i]->value, static_cast<int>(i));
                    CHECK_EQ(objects[i]->thread, id);
                    objects[i]->~TestObject();
                    allocator.free(objects[i]);
                    objects[i] = nullptr;
                }
            }

            // Allocate more
            for (int i = 0; i < objectsPerIteration / 2; ++i)
            {
                TestObject* obj = allocator.allocate();
                if (obj)
                {
                    new (obj) TestObject(i + objectsPerIteration, i * 3.0, id);
                    objects.push_back(obj);
                }
            }

            // Deallocate all remaining
            for (size_t i = 0; i < objects.size(); ++i)
            {
                if (objects[i])
                {
                    CHECK_EQ(objects[i]->value, static_cast<int>(i));
                    CHECK_EQ(objects[i]->thread, id);
                    objects[i]->~TestObject();
                    allocator.free(objects[i]);
                    objects[i] = nullptr;
                }
            }

            CHECK(allocator.getNumPages() < (numThreads * objectsPerIteration * 2) / blocksPerPage);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back(threadFunc);
    }

    for (auto& thread : threads)
    {
        thread.join();
    }
}

#if 0
// This perf test doesn't verify any functionality but can be enabled to compare
// block allocator perf to standard new/delete
TEST_CASE("block-allocator-performance")
{
    constexpr int numAllocations = 1000000;

    SUBCASE("block-allocator-performance")
    {
        BlockAllocator<TestObject> allocator(1000000);

        std::vector<TestObject*> objects;
        objects.reserve(numAllocations);

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < numAllocations; ++i)
        {
            objects.push_back(allocator.allocate());
        }

        for (auto obj : objects)
        {
            allocator.free(obj);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        MESSAGE("BlockAllocator: ", numAllocations, " allocations in ", duration.count(), " μs");
    }

    SUBCASE("standard-new-delete-performance")
    {
        std::vector<TestObject*> objects;
        objects.reserve(numAllocations);

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < numAllocations; ++i)
        {
            objects.push_back(new TestObject());
        }

        for (auto obj : objects)
        {
            delete obj;
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        MESSAGE("Standard new/delete: ", numAllocations, " allocations in ", duration.count(), " μs");
    }
}
#endif

// Test the macro system
class TestMacroClass
{
    SLANG_RHI_DECLARE_BLOCK_ALLOCATED(TestMacroClass)

public:
    int value = 0;
    TestMacroClass() = default;
    TestMacroClass(int v)
        : value(v)
    {
    }

    // For testing - expose allocator
    static BlockAllocator<TestMacroClass>& getAllocator() { return s_allocator; }
};

SLANG_RHI_IMPLEMENT_BLOCK_ALLOCATED(TestMacroClass, 32)

TEST_CASE("block-allocator-macro-system")
{
    SUBCASE("basic-allocation-with-macro")
    {
        TestMacroClass* obj = new TestMacroClass(42);
        REQUIRE(obj != nullptr);
        CHECK(obj->value == 42);
        delete obj;
    }

    SUBCASE("ownership-with-macro")
    {
        TestMacroClass* obj = new TestMacroClass(100);
        REQUIRE(obj != nullptr);

        // The static allocator should own this
        CHECK(TestMacroClass::getAllocator().owns(obj));

        delete obj;
    }

    SUBCASE("multiple-allocations-with-macro")
    {
        std::vector<TestMacroClass*> objects;

        for (int i = 0; i < 100; ++i)
        {
            objects.push_back(new TestMacroClass(i));
        }

        for (int i = 0; i < 100; ++i)
        {
            CHECK(objects[i]->value == i);
        }

        for (auto obj : objects)
        {
            delete obj;
        }
    }
}
