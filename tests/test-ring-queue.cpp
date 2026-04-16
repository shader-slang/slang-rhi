#include "testing.h"

#include "../src/core/ring-queue.h"

#include <string>
#include <vector>

using namespace rhi;

namespace {

// Helper struct for deferred delete simulation
struct DeferredDelete
{
    uint64_t submissionID;
    int resourceID;
};

} // namespace

TEST_CASE("ring-queue")
{
    SUBCASE("default-construction")
    {
        RingQueue<int> queue;
        CHECK(queue.empty());
        CHECK(queue.size() == 0);
        CHECK(queue.capacity() > 0);
    }

    SUBCASE("construction-with-capacity")
    {
        RingQueue<int> queue(128);
        CHECK(queue.empty());
        CHECK(queue.capacity() == 128);
    }

    SUBCASE("push-and-pop")
    {
        RingQueue<int> queue(4);
        queue.push(1);
        queue.push(2);
        queue.push(3);

        CHECK(queue.size() == 3);
        CHECK(queue.front() == 1);
        CHECK(queue.back() == 3);

        queue.pop();
        CHECK(queue.size() == 2);
        CHECK(queue.front() == 2);

        queue.pop();
        CHECK(queue.size() == 1);
        CHECK(queue.front() == 3);

        queue.pop();
        CHECK(queue.empty());
    }

    SUBCASE("push-rvalue")
    {
        RingQueue<std::string> queue(4);
        std::string s = "hello";
        queue.push(std::move(s));
        CHECK(queue.front() == "hello");
    }

    SUBCASE("emplace")
    {
        RingQueue<std::pair<int, std::string>> queue(4);
        auto& ref = queue.emplace(42, "hello");
        CHECK(ref.first == 42);
        CHECK(ref.second == "hello");
        CHECK(queue.size() == 1);
        CHECK(queue.front().first == 42);
    }

    SUBCASE("wraparound")
    {
        RingQueue<int> queue(4);

        // Fill the queue
        queue.push(1);
        queue.push(2);
        queue.push(3);
        queue.push(4);
        CHECK(queue.size() == 4);

        // Pop some elements
        queue.pop();
        queue.pop();
        CHECK(queue.size() == 2);
        CHECK(queue.front() == 3);

        // Push more elements (should wrap around)
        queue.push(5);
        queue.push(6);
        CHECK(queue.size() == 4);

        // Verify order
        CHECK(queue.front() == 3);
        queue.pop();
        CHECK(queue.front() == 4);
        queue.pop();
        CHECK(queue.front() == 5);
        queue.pop();
        CHECK(queue.front() == 6);
        queue.pop();
        CHECK(queue.empty());
    }

    SUBCASE("growth")
    {
        RingQueue<int> queue(2);
        CHECK(queue.capacity() == 2);

        queue.push(1);
        queue.push(2);
        CHECK(queue.capacity() == 2);

        // This should trigger growth
        queue.push(3);
        CHECK(queue.capacity() == 4);
        CHECK(queue.size() == 3);

        // Verify elements are preserved after growth
        CHECK(queue.front() == 1);
        queue.pop();
        CHECK(queue.front() == 2);
        queue.pop();
        CHECK(queue.front() == 3);
    }

    SUBCASE("growth-with-wraparound")
    {
        RingQueue<int> queue(4);

        // Create a wraparound situation
        queue.push(1);
        queue.push(2);
        queue.push(3);
        queue.push(4);
        queue.pop();
        queue.pop();
        queue.push(5);
        queue.push(6);

        // Now queue contains [3, 4, 5, 6] with wraparound
        CHECK(queue.size() == 4);

        // This should trigger growth and compaction
        queue.push(7);
        CHECK(queue.capacity() == 8);
        CHECK(queue.size() == 5);

        // Verify elements are in correct order after compaction
        std::vector<int> expected = {3, 4, 5, 6, 7};
        for (int val : expected)
        {
            CHECK(queue.front() == val);
            queue.pop();
        }
        CHECK(queue.empty());
    }

    SUBCASE("clear")
    {
        RingQueue<int> queue(4);
        queue.push(1);
        queue.push(2);
        queue.push(3);

        queue.clear();
        CHECK(queue.empty());
        CHECK(queue.size() == 0);

        // Should be able to push again
        queue.push(10);
        CHECK(queue.front() == 10);
    }

    SUBCASE("reserve")
    {
        RingQueue<int> queue(4);
        queue.push(1);
        queue.push(2);

        queue.reserve(16);
        CHECK(queue.capacity() >= 16);
        CHECK(queue.size() == 2);
        CHECK(queue.front() == 1);
    }

    SUBCASE("reserve-smaller-than-current")
    {
        RingQueue<int> queue(16);
        queue.push(1);
        queue.push(2);

        size_t old_capacity = queue.capacity();
        queue.reserve(4);
        // Should not shrink
        CHECK(queue.capacity() == old_capacity);
    }

    SUBCASE("iterator")
    {
        RingQueue<int> queue(4);
        queue.push(1);
        queue.push(2);
        queue.push(3);

        std::vector<int> values;
        for (int val : queue)
        {
            values.push_back(val);
        }

        CHECK(values.size() == 3);
        CHECK(values[0] == 1);
        CHECK(values[1] == 2);
        CHECK(values[2] == 3);
    }

    SUBCASE("iterator-with-wraparound")
    {
        RingQueue<int> queue(4);

        // Create wraparound
        queue.push(1);
        queue.push(2);
        queue.push(3);
        queue.push(4);
        queue.pop();
        queue.pop();
        queue.push(5);
        queue.push(6);

        std::vector<int> values;
        for (int val : queue)
        {
            values.push_back(val);
        }

        CHECK(values.size() == 4);
        CHECK(values[0] == 3);
        CHECK(values[1] == 4);
        CHECK(values[2] == 5);
        CHECK(values[3] == 6);
    }

    SUBCASE("const-iterator")
    {
        RingQueue<int> queue(4);
        queue.push(1);
        queue.push(2);
        queue.push(3);

        const RingQueue<int>& const_queue = queue;
        std::vector<int> values;
        for (auto it = const_queue.cbegin(); it != const_queue.cend(); ++it)
        {
            values.push_back(*it);
        }

        CHECK(values.size() == 3);
        CHECK(values[0] == 1);
        CHECK(values[1] == 2);
        CHECK(values[2] == 3);
    }

    SUBCASE("copy-construction")
    {
        RingQueue<int> queue1(4);
        queue1.push(1);
        queue1.push(2);
        queue1.push(3);

        RingQueue<int> queue2(queue1);
        CHECK(queue2.size() == 3);
        CHECK(queue2.front() == 1);

        // Modify original, copy should be independent
        queue1.pop();
        CHECK(queue2.size() == 3);
        CHECK(queue2.front() == 1);
    }

    SUBCASE("copy-construction-with-wraparound")
    {
        RingQueue<int> queue1(4);

        // Create wraparound
        queue1.push(1);
        queue1.push(2);
        queue1.push(3);
        queue1.push(4);
        queue1.pop();
        queue1.pop();
        queue1.push(5);
        queue1.push(6);

        RingQueue<int> queue2(queue1);
        CHECK(queue2.size() == 4);

        // Verify elements are compacted in copy
        std::vector<int> expected = {3, 4, 5, 6};
        for (int val : expected)
        {
            CHECK(queue2.front() == val);
            queue2.pop();
        }
    }

    SUBCASE("move-construction")
    {
        RingQueue<int> queue1(4);
        queue1.push(1);
        queue1.push(2);
        queue1.push(3);

        RingQueue<int> queue2(std::move(queue1));
        CHECK(queue2.size() == 3);
        CHECK(queue2.front() == 1);
        // Testing moved-from state
        CHECK(queue1.empty());
    }

    SUBCASE("copy-assignment")
    {
        RingQueue<int> queue1(4);
        queue1.push(1);
        queue1.push(2);

        RingQueue<int> queue2(8);
        queue2.push(10);
        queue2.push(20);
        queue2.push(30);

        queue2 = queue1;
        CHECK(queue2.size() == 2);
        CHECK(queue2.front() == 1);
    }

    SUBCASE("move-assignment")
    {
        RingQueue<int> queue1(4);
        queue1.push(1);
        queue1.push(2);

        RingQueue<int> queue2(8);
        queue2.push(10);

        queue2 = std::move(queue1);
        CHECK(queue2.size() == 2);
        CHECK(queue2.front() == 1);
        // Testing moved-from state
        CHECK(queue1.empty());
    }

    SUBCASE("head-reset-on-empty")
    {
        RingQueue<int> queue(4);

        // Push and pop to move head forward
        queue.push(1);
        queue.push(2);
        queue.pop();
        queue.pop();

        // Queue should reset indices when empty
        CHECK(queue.empty());

        // Push again - should work correctly
        queue.push(10);
        CHECK(queue.front() == 10);
        CHECK(queue.size() == 1);
    }

    SUBCASE("stress-test-no-reallocation-after-growth")
    {
        RingQueue<int> queue(4);

        // Grow to a certain size
        for (int i = 0; i < 100; ++i)
        {
            queue.push(i);
        }

        size_t capacity_after_growth = queue.capacity();

        // Clear and refill multiple times
        for (int round = 0; round < 10; ++round)
        {
            queue.clear();
            for (int i = 0; i < 50; ++i)
            {
                queue.push(i);
                if (i % 2 == 0)
                    queue.pop();
            }
            // Capacity should never increase since we're under the max
            CHECK(queue.capacity() == capacity_after_growth);
        }
    }

    SUBCASE("deferred-delete-simulation")
    {
        // Simulate the actual use case from d3d12-command.cpp
        RingQueue<DeferredDelete> deferredDeletes(64);

        // Simulate multiple submissions with deferred deletes
        uint64_t lastSubmittedID = 0;
        uint64_t lastFinishedID = 0;

        auto deferDelete = [&](int resourceID)
        {
            deferredDeletes.push({lastSubmittedID, resourceID});
        };

        auto executeDeferredDeletes = [&]()
        {
            while (!deferredDeletes.empty() && deferredDeletes.front().submissionID <= lastFinishedID)
            {
                deferredDeletes.pop();
            }
        };

        // Frame 1: submit work, defer some deletes
        lastSubmittedID = 1;
        deferDelete(100);
        deferDelete(101);

        // Frame 2: submit more work
        lastSubmittedID = 2;
        deferDelete(102);

        // Frame 3: GPU finished frame 1
        lastFinishedID = 1;
        executeDeferredDeletes();
        CHECK(deferredDeletes.size() == 1);
        CHECK(deferredDeletes.front().resourceID == 102);

        // Frame 4: submit work
        lastSubmittedID = 3;
        deferDelete(103);
        deferDelete(104);

        // Frame 5: GPU finished frame 3
        lastFinishedID = 3;
        executeDeferredDeletes();
        CHECK(deferredDeletes.empty());
    }
}
