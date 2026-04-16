#include "testing.h"
#include "../src/core/static_vector.h"

#include <string>
#include <memory>
#include <vector>

using namespace rhi;

namespace {

// Test struct to track construction/destruction for lifetime testing
struct LifetimeTracker
{
    static int s_construct_count;
    static int s_destruct_count;
    static int s_copy_count;
    static int s_move_count;

    int value;

    static void reset_counters()
    {
        s_construct_count = 0;
        s_destruct_count = 0;
        s_copy_count = 0;
        s_move_count = 0;
    }

    LifetimeTracker()
        : value(0)
    {
        ++s_construct_count;
    }

    explicit LifetimeTracker(int v)
        : value(v)
    {
        ++s_construct_count;
    }

    LifetimeTracker(const LifetimeTracker& other)
        : value(other.value)
    {
        ++s_construct_count;
        ++s_copy_count;
    }

    LifetimeTracker(LifetimeTracker&& other) noexcept
        : value(other.value)
    {
        ++s_construct_count;
        ++s_move_count;
        other.value = -1;
    }

    LifetimeTracker& operator=(const LifetimeTracker& other)
    {
        value = other.value;
        ++s_copy_count;
        return *this;
    }

    LifetimeTracker& operator=(LifetimeTracker&& other) noexcept
    {
        value = other.value;
        ++s_move_count;
        other.value = -1;
        return *this;
    }

    ~LifetimeTracker() { ++s_destruct_count; }
};

int LifetimeTracker::s_construct_count = 0;
int LifetimeTracker::s_destruct_count = 0;
int LifetimeTracker::s_copy_count = 0;
int LifetimeTracker::s_move_count = 0;

} // namespace

// Non-default-constructible type
struct NoDefaultCtor
{
    int value;
    explicit NoDefaultCtor(int v)
        : value(v)
    {
    }
};

TEST_CASE("static_vector")
{
    SUBCASE("default-construction")
    {
        static_vector<int, 10> vec;
        CHECK(vec.empty());
        CHECK(vec.size() == 0);
        CHECK(vec.capacity() == 10);
        CHECK(vec.max_size() == 10);
    }

    SUBCASE("count-construction")
    {
        static_vector<int, 10> vec(5);
        CHECK(vec.size() == 5);
        for (size_t i = 0; i < vec.size(); ++i)
            CHECK(vec[i] == 0);
    }

    SUBCASE("count-value-construction")
    {
        static_vector<int, 10> vec(5, 42);
        CHECK(vec.size() == 5);
        for (size_t i = 0; i < vec.size(); ++i)
            CHECK(vec[i] == 42);
    }

    SUBCASE("initializer-list-construction")
    {
        static_vector<int, 10> vec = {1, 2, 3, 4, 5};
        CHECK(vec.size() == 5);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3);
        CHECK(vec[3] == 4);
        CHECK(vec[4] == 5);
    }

    SUBCASE("iterator-range-construction")
    {
        std::vector<int> source = {10, 20, 30, 40};
        static_vector<int, 10> vec(source.begin(), source.end());
        CHECK(vec.size() == 4);
        CHECK(vec[0] == 10);
        CHECK(vec[1] == 20);
        CHECK(vec[2] == 30);
        CHECK(vec[3] == 40);
    }

    SUBCASE("iterator-range-construction-from-array")
    {
        int arr[] = {1, 2, 3, 4, 5};
        static_vector<int, 10> vec(std::begin(arr), std::end(arr));
        CHECK(vec.size() == 5);
        for (int i = 0; i < 5; ++i)
            CHECK(vec[i] == i + 1);
    }

    SUBCASE("push_back-lvalue")
    {
        static_vector<int, 10> vec;
        int value = 42;
        vec.push_back(value);
        CHECK(vec.size() == 1);
        CHECK(vec[0] == 42);
    }

    SUBCASE("push_back-rvalue")
    {
        static_vector<std::string, 10> vec;
        vec.push_back("hello");
        CHECK(vec.size() == 1);
        CHECK(vec[0] == "hello");
    }

    SUBCASE("emplace_back")
    {
        static_vector<std::pair<int, std::string>, 10> vec;
        vec.emplace_back(42, "hello");
        CHECK(vec.size() == 1);
        CHECK(vec[0].first == 42);
        CHECK(vec[0].second == "hello");
    }

    SUBCASE("emplace_back-returns-reference")
    {
        static_vector<int, 10> vec;
        int& ref = vec.emplace_back(42);
        CHECK(ref == 42);
        ref = 100;
        CHECK(vec[0] == 100);
    }

    SUBCASE("pop_back")
    {
        static_vector<int, 10> vec = {1, 2, 3};
        vec.pop_back();
        CHECK(vec.size() == 2);
        CHECK(vec.back() == 2);
    }

    SUBCASE("front-and-back")
    {
        static_vector<int, 10> vec = {1, 2, 3};
        CHECK(vec.front() == 1);
        CHECK(vec.back() == 3);

        vec.front() = 10;
        vec.back() = 30;
        CHECK(vec[0] == 10);
        CHECK(vec[2] == 30);
    }

    SUBCASE("const-front-and-back")
    {
        const static_vector<int, 10> vec = {1, 2, 3};
        CHECK(vec.front() == 1);
        CHECK(vec.back() == 3);
    }

    SUBCASE("data-access")
    {
        static_vector<int, 10> vec = {1, 2, 3};
        int* ptr = vec.data();
        CHECK(ptr[0] == 1);
        CHECK(ptr[1] == 2);
        CHECK(ptr[2] == 3);
    }

    SUBCASE("const-data-access")
    {
        const static_vector<int, 10> vec = {1, 2, 3};
        const int* ptr = vec.data();
        CHECK(ptr[0] == 1);
    }

    SUBCASE("clear")
    {
        LifetimeTracker::reset_counters();
        {
            static_vector<LifetimeTracker, 10> vec;
            vec.emplace_back(1);
            vec.emplace_back(2);
            vec.emplace_back(3);
            CHECK(vec.size() == 3);
            CHECK(LifetimeTracker::s_construct_count == 3);
            CHECK(LifetimeTracker::s_destruct_count == 0);

            vec.clear();
            CHECK(vec.empty());
            CHECK(LifetimeTracker::s_destruct_count == 3);
        }
    }

    SUBCASE("resize-grow")
    {
        static_vector<int, 10> vec = {1, 2};
        vec.resize(5);
        CHECK(vec.size() == 5);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 0);
        CHECK(vec[3] == 0);
        CHECK(vec[4] == 0);
    }

    SUBCASE("resize-grow-with-value")
    {
        static_vector<int, 10> vec = {1, 2};
        vec.resize(5, 42);
        CHECK(vec.size() == 5);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 42);
        CHECK(vec[3] == 42);
        CHECK(vec[4] == 42);
    }

    SUBCASE("resize-shrink")
    {
        LifetimeTracker::reset_counters();
        {
            static_vector<LifetimeTracker, 10> vec;
            vec.emplace_back(1);
            vec.emplace_back(2);
            vec.emplace_back(3);
            vec.emplace_back(4);
            vec.emplace_back(5);
            CHECK(LifetimeTracker::s_destruct_count == 0);

            vec.resize(2);
            CHECK(vec.size() == 2);
            CHECK(vec[0].value == 1);
            CHECK(vec[1].value == 2);
            CHECK(LifetimeTracker::s_destruct_count == 3);
        }
    }

    SUBCASE("resize-same-size")
    {
        static_vector<int, 10> vec = {1, 2, 3};
        vec.resize(3);
        CHECK(vec.size() == 3);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3);
    }

    SUBCASE("iterators")
    {
        static_vector<int, 10> vec = {1, 2, 3, 4, 5};

        int sum = 0;
        for (auto it = vec.begin(); it != vec.end(); ++it)
            sum += *it;
        CHECK(sum == 15);

        sum = 0;
        for (const auto& val : vec)
            sum += val;
        CHECK(sum == 15);
    }

    SUBCASE("const-iterators")
    {
        const static_vector<int, 10> vec = {1, 2, 3};
        int sum = 0;
        for (auto it = vec.cbegin(); it != vec.cend(); ++it)
            sum += *it;
        CHECK(sum == 6);
    }

    SUBCASE("reverse-iterators")
    {
        static_vector<int, 10> vec = {1, 2, 3, 4, 5};

        // Test rbegin/rend
        std::vector<int> reversed;
        for (auto it = vec.rbegin(); it != vec.rend(); ++it)
            reversed.push_back(*it);
        CHECK(reversed.size() == 5);
        CHECK(reversed[0] == 5);
        CHECK(reversed[1] == 4);
        CHECK(reversed[2] == 3);
        CHECK(reversed[3] == 2);
        CHECK(reversed[4] == 1);

        // Test modification through reverse iterator
        *vec.rbegin() = 50;
        CHECK(vec.back() == 50);
    }

    SUBCASE("const-reverse-iterators")
    {
        const static_vector<int, 10> vec = {1, 2, 3};
        std::vector<int> reversed;
        for (auto it = vec.crbegin(); it != vec.crend(); ++it)
            reversed.push_back(*it);
        CHECK(reversed.size() == 3);
        CHECK(reversed[0] == 3);
        CHECK(reversed[1] == 2);
        CHECK(reversed[2] == 1);
    }

    SUBCASE("copy-construction")
    {
        LifetimeTracker::reset_counters();
        {
            static_vector<LifetimeTracker, 10> vec1;
            vec1.emplace_back(1);
            vec1.emplace_back(2);
            vec1.emplace_back(3);

            static_vector<LifetimeTracker, 10> vec2(vec1);
            CHECK(vec2.size() == 3);
            CHECK(vec2[0].value == 1);
            CHECK(vec2[1].value == 2);
            CHECK(vec2[2].value == 3);

            // Original unchanged
            CHECK(vec1[0].value == 1);
            CHECK(vec1[1].value == 2);
            CHECK(vec1[2].value == 3);

            CHECK(LifetimeTracker::s_copy_count == 3);
        }
    }

    SUBCASE("move-construction")
    {
        LifetimeTracker::reset_counters();
        {
            static_vector<LifetimeTracker, 10> vec1;
            vec1.emplace_back(1);
            vec1.emplace_back(2);
            vec1.emplace_back(3);

            static_vector<LifetimeTracker, 10> vec2(std::move(vec1));
            CHECK(vec2.size() == 3);
            CHECK(vec2[0].value == 1);
            CHECK(vec2[1].value == 2);
            CHECK(vec2[2].value == 3);

            // Original should be cleared
            CHECK(vec1.empty());

            CHECK(LifetimeTracker::s_move_count == 3);
        }
    }

    SUBCASE("copy-assignment")
    {
        static_vector<int, 10> vec1 = {1, 2, 3};
        static_vector<int, 10> vec2 = {4, 5};

        vec2 = vec1;
        CHECK(vec2.size() == 3);
        CHECK(vec2[0] == 1);
        CHECK(vec2[1] == 2);
        CHECK(vec2[2] == 3);
    }

    SUBCASE("move-assignment")
    {
        static_vector<int, 10> vec1 = {1, 2, 3};
        static_vector<int, 10> vec2 = {4, 5};

        vec2 = std::move(vec1);
        CHECK(vec2.size() == 3);
        CHECK(vec2[0] == 1);
        CHECK(vec2[1] == 2);
        CHECK(vec2[2] == 3);
        CHECK(vec1.empty());
    }

    SUBCASE("erase-single")
    {
        static_vector<int, 10> vec = {1, 2, 3, 4, 5};
        auto it = vec.erase(vec.begin() + 2);
        CHECK(vec.size() == 4);
        CHECK(*it == 4);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 4);
        CHECK(vec[3] == 5);
    }

    SUBCASE("erase-range")
    {
        static_vector<int, 10> vec = {1, 2, 3, 4, 5};
        auto it = vec.erase(vec.begin() + 1, vec.begin() + 4);
        CHECK(vec.size() == 2);
        CHECK(*it == 5);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 5);
    }

    SUBCASE("erase-range-empty")
    {
        static_vector<int, 10> vec = {1, 2, 3};
        auto it = vec.erase(vec.begin() + 1, vec.begin() + 1);
        CHECK(vec.size() == 3);
        CHECK(*it == 2);
    }

    SUBCASE("non-default-constructible-type")
    {
        static_vector<NoDefaultCtor, 10> vec;
        vec.emplace_back(42);
        vec.push_back(NoDefaultCtor(100));
        CHECK(vec.size() == 2);
        CHECK(vec[0].value == 42);
        CHECK(vec[1].value == 100);
    }

    SUBCASE("lifetime-destruction-order")
    {
        LifetimeTracker::reset_counters();
        {
            static_vector<LifetimeTracker, 10> vec;
            vec.emplace_back(1);
            vec.emplace_back(2);
            vec.emplace_back(3);
            CHECK(LifetimeTracker::s_construct_count == 3);
        }
        CHECK(LifetimeTracker::s_destruct_count == 3);
    }

    SUBCASE("pop_back-destroys-element")
    {
        LifetimeTracker::reset_counters();
        {
            static_vector<LifetimeTracker, 10> vec;
            vec.emplace_back(1);
            vec.emplace_back(2);
            CHECK(LifetimeTracker::s_destruct_count == 0);

            vec.pop_back();
            CHECK(LifetimeTracker::s_destruct_count == 1);
            CHECK(vec.size() == 1);
            CHECK(vec[0].value == 1);
        }
        CHECK(LifetimeTracker::s_destruct_count == 2);
    }

    SUBCASE("full-capacity")
    {
        static_vector<int, 5> vec;
        for (int i = 0; i < 5; ++i)
            vec.push_back(i);
        CHECK(vec.size() == 5);
        CHECK(vec.size() == vec.capacity());

        for (int i = 0; i < 5; ++i)
            CHECK(vec[i] == i);
    }

    SUBCASE("trivial-type-operations")
    {
        static_vector<int, 100> vec;

        // Fill
        for (int i = 0; i < 100; ++i)
            vec.push_back(i);
        CHECK(vec.size() == 100);

        // Verify
        for (int i = 0; i < 100; ++i)
            CHECK(vec[i] == i);

        // Clear and refill
        vec.clear();
        CHECK(vec.empty());

        for (int i = 99; i >= 0; --i)
            vec.push_back(i);

        for (int i = 0; i < 100; ++i)
            CHECK(vec[i] == 99 - i);
    }

    SUBCASE("string-operations")
    {
        static_vector<std::string, 10> vec;
        vec.push_back("hello");
        vec.push_back("world");
        vec.emplace_back(10, 'x'); // string of 10 'x' characters

        CHECK(vec.size() == 3);
        CHECK(vec[0] == "hello");
        CHECK(vec[1] == "world");
        CHECK(vec[2] == "xxxxxxxxxx");

        vec.pop_back();
        CHECK(vec.size() == 2);
        CHECK(vec.back() == "world");
    }

    SUBCASE("insert-lvalue-at-end")
    {
        static_vector<int, 10> vec = {1, 2, 3};
        int value = 4;
        auto it = vec.insert(vec.end(), value);
        CHECK(vec.size() == 4);
        CHECK(*it == 4);
        CHECK(vec[3] == 4);
    }

    SUBCASE("insert-lvalue-at-beginning")
    {
        static_vector<int, 10> vec = {2, 3, 4};
        int value = 1;
        auto it = vec.insert(vec.begin(), value);
        CHECK(vec.size() == 4);
        CHECK(*it == 1);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3);
        CHECK(vec[3] == 4);
    }

    SUBCASE("insert-lvalue-in-middle")
    {
        static_vector<int, 10> vec = {1, 2, 4, 5};
        int value = 3;
        auto it = vec.insert(vec.begin() + 2, value);
        CHECK(vec.size() == 5);
        CHECK(*it == 3);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3);
        CHECK(vec[3] == 4);
        CHECK(vec[4] == 5);
    }

    SUBCASE("insert-rvalue")
    {
        static_vector<std::string, 10> vec = {"hello", "world"};
        auto it = vec.insert(vec.begin() + 1, "beautiful");
        CHECK(vec.size() == 3);
        CHECK(*it == "beautiful");
        CHECK(vec[0] == "hello");
        CHECK(vec[1] == "beautiful");
        CHECK(vec[2] == "world");
    }

    SUBCASE("assign-count-value")
    {
        static_vector<int, 10> vec = {1, 2, 3};
        vec.assign(5, 42);
        CHECK(vec.size() == 5);
        for (size_t i = 0; i < vec.size(); ++i)
            CHECK(vec[i] == 42);
    }

    SUBCASE("assign-iterator-range")
    {
        static_vector<int, 10> vec = {1, 2, 3};
        std::vector<int> source = {10, 20, 30, 40, 50};
        vec.assign(source.begin(), source.end());
        CHECK(vec.size() == 5);
        CHECK(vec[0] == 10);
        CHECK(vec[1] == 20);
        CHECK(vec[2] == 30);
        CHECK(vec[3] == 40);
        CHECK(vec[4] == 50);
    }

    SUBCASE("assign-initializer-list")
    {
        static_vector<int, 10> vec = {1, 2, 3};
        vec.assign({100, 200});
        CHECK(vec.size() == 2);
        CHECK(vec[0] == 100);
        CHECK(vec[1] == 200);
    }

    SUBCASE("swap-same-size")
    {
        static_vector<int, 10> vec1 = {1, 2, 3};
        static_vector<int, 10> vec2 = {4, 5, 6};
        vec1.swap(vec2);
        CHECK(vec1.size() == 3);
        CHECK(vec2.size() == 3);
        CHECK(vec1[0] == 4);
        CHECK(vec1[1] == 5);
        CHECK(vec1[2] == 6);
        CHECK(vec2[0] == 1);
        CHECK(vec2[1] == 2);
        CHECK(vec2[2] == 3);
    }

    SUBCASE("swap-different-sizes")
    {
        static_vector<int, 10> vec1 = {1, 2, 3, 4, 5};
        static_vector<int, 10> vec2 = {10, 20};
        vec1.swap(vec2);
        CHECK(vec1.size() == 2);
        CHECK(vec2.size() == 5);
        CHECK(vec1[0] == 10);
        CHECK(vec1[1] == 20);
        CHECK(vec2[0] == 1);
        CHECK(vec2[1] == 2);
        CHECK(vec2[2] == 3);
        CHECK(vec2[3] == 4);
        CHECK(vec2[4] == 5);
    }

    SUBCASE("swap-with-empty")
    {
        static_vector<int, 10> vec1 = {1, 2, 3};
        static_vector<int, 10> vec2;
        vec1.swap(vec2);
        CHECK(vec1.empty());
        CHECK(vec2.size() == 3);
        CHECK(vec2[0] == 1);
        CHECK(vec2[1] == 2);
        CHECK(vec2[2] == 3);
    }

    SUBCASE("swap-self")
    {
        static_vector<int, 10> vec = {1, 2, 3};
        vec.swap(vec);
        CHECK(vec.size() == 3);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3);
    }

    SUBCASE("swap-with-lifetime-tracking")
    {
        LifetimeTracker::reset_counters();
        {
            static_vector<LifetimeTracker, 10> vec1;
            vec1.emplace_back(1);
            vec1.emplace_back(2);

            static_vector<LifetimeTracker, 10> vec2;
            vec2.emplace_back(10);
            vec2.emplace_back(20);
            vec2.emplace_back(30);

            vec1.swap(vec2);

            CHECK(vec1.size() == 3);
            CHECK(vec1[0].value == 10);
            CHECK(vec1[1].value == 20);
            CHECK(vec1[2].value == 30);

            CHECK(vec2.size() == 2);
            CHECK(vec2[0].value == 1);
            CHECK(vec2[1].value == 2);
        }
        // All elements should be properly destroyed
        CHECK(LifetimeTracker::s_construct_count == LifetimeTracker::s_destruct_count);
    }

    SUBCASE("insert-with-lifetime-tracking")
    {
        LifetimeTracker::reset_counters();
        {
            static_vector<LifetimeTracker, 10> vec;
            vec.emplace_back(1);
            vec.emplace_back(3);

            LifetimeTracker value(2);
            vec.insert(vec.begin() + 1, value);

            CHECK(vec.size() == 3);
            CHECK(vec[0].value == 1);
            CHECK(vec[1].value == 2);
            CHECK(vec[2].value == 3);
        }
        CHECK(LifetimeTracker::s_construct_count == LifetimeTracker::s_destruct_count);
    }

    // POD optimization tests - these exercise the memmove/memcpy fast paths
    SUBCASE("pod-erase-first")
    {
        static_vector<int, 10> vec = {1, 2, 3, 4, 5};
        vec.erase(vec.begin());
        CHECK(vec.size() == 4);
        CHECK(vec[0] == 2);
        CHECK(vec[1] == 3);
        CHECK(vec[2] == 4);
        CHECK(vec[3] == 5);
    }

    SUBCASE("pod-erase-last")
    {
        static_vector<int, 10> vec = {1, 2, 3, 4, 5};
        vec.erase(vec.end() - 1);
        CHECK(vec.size() == 4);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3);
        CHECK(vec[3] == 4);
    }

    SUBCASE("pod-erase-range-all")
    {
        static_vector<int, 10> vec = {1, 2, 3, 4, 5};
        vec.erase(vec.begin(), vec.end());
        CHECK(vec.empty());
    }

    SUBCASE("pod-erase-range-from-start")
    {
        static_vector<int, 10> vec = {1, 2, 3, 4, 5};
        vec.erase(vec.begin(), vec.begin() + 3);
        CHECK(vec.size() == 2);
        CHECK(vec[0] == 4);
        CHECK(vec[1] == 5);
    }

    SUBCASE("pod-erase-range-to-end")
    {
        static_vector<int, 10> vec = {1, 2, 3, 4, 5};
        vec.erase(vec.begin() + 2, vec.end());
        CHECK(vec.size() == 2);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
    }

    SUBCASE("pod-insert-into-empty")
    {
        static_vector<int, 10> vec;
        vec.insert(vec.begin(), 42);
        CHECK(vec.size() == 1);
        CHECK(vec[0] == 42);
    }

    SUBCASE("pod-insert-multiple-at-beginning")
    {
        static_vector<int, 10> vec = {3, 4, 5};
        vec.insert(vec.begin(), 2);
        vec.insert(vec.begin(), 1);
        CHECK(vec.size() == 5);
        for (int i = 0; i < 5; ++i)
            CHECK(vec[i] == i + 1);
    }

    SUBCASE("pod-insert-self-reference")
    {
        // Test inserting an element that references itself (the copy should happen before shift)
        static_vector<int, 10> vec = {1, 2, 3};
        vec.insert(vec.begin() + 1, vec[0]);
        CHECK(vec.size() == 4);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 1);
        CHECK(vec[2] == 2);
        CHECK(vec[3] == 3);
    }

    SUBCASE("pod-copy-construct-large")
    {
        static_vector<int, 100> vec1;
        for (int i = 0; i < 100; ++i)
            vec1.push_back(i);

        static_vector<int, 100> vec2(vec1);
        CHECK(vec2.size() == 100);
        for (int i = 0; i < 100; ++i)
            CHECK(vec2[i] == i);
    }

    SUBCASE("pod-move-construct-large")
    {
        static_vector<int, 100> vec1;
        for (int i = 0; i < 100; ++i)
            vec1.push_back(i);

        static_vector<int, 100> vec2(std::move(vec1));
        CHECK(vec2.size() == 100);
        for (int i = 0; i < 100; ++i)
            CHECK(vec2[i] == i);
        CHECK(vec1.empty());
    }
}
