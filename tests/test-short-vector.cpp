#include "testing.h"

#include "../src/core/short_vector.h"

#include <memory>
#include <string>
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

TEST_CASE("short_vector")
{
    SUBCASE("default-construction")
    {
        short_vector<int, 4> vec;
        CHECK(vec.empty());
        CHECK(vec.size() == 0);
        CHECK(vec.capacity() == 4);
        CHECK(vec.is_inline());
    }

    SUBCASE("count-construction")
    {
        short_vector<int, 4> vec(3);
        CHECK(vec.size() == 3);
        CHECK(vec.is_inline());
        for (size_t i = 0; i < vec.size(); ++i)
            CHECK(vec[i] == 0);
    }

    SUBCASE("count-value-construction")
    {
        short_vector<int, 4> vec(3, 42);
        CHECK(vec.size() == 3);
        CHECK(vec.is_inline());
        for (size_t i = 0; i < vec.size(); ++i)
            CHECK(vec[i] == 42);
    }

    SUBCASE("initializer-list-construction")
    {
        short_vector<int, 4> vec = {1, 2, 3};
        CHECK(vec.size() == 3);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3);
        CHECK(vec.is_inline());
    }

    SUBCASE("iterator-range-construction")
    {
        std::vector<int> source = {10, 20, 30, 40};
        short_vector<int, 8> vec(source.begin(), source.end());
        CHECK(vec.size() == 4);
        CHECK(vec[0] == 10);
        CHECK(vec[1] == 20);
        CHECK(vec[2] == 30);
        CHECK(vec[3] == 40);
    }

    SUBCASE("push_back-lvalue")
    {
        short_vector<int, 4> vec;
        int value = 42;
        vec.push_back(value);
        CHECK(vec.size() == 1);
        CHECK(vec[0] == 42);
        CHECK(vec.is_inline());
    }

    SUBCASE("push_back-rvalue")
    {
        short_vector<std::string, 4> vec;
        vec.push_back("hello");
        CHECK(vec.size() == 1);
        CHECK(vec[0] == "hello");
    }

    SUBCASE("push_back-triggers-growth")
    {
        short_vector<int, 2> vec;
        vec.push_back(1);
        vec.push_back(2);
        CHECK(vec.is_inline());

        vec.push_back(3);
        CHECK(!vec.is_inline());
        CHECK(vec.size() == 3);
        CHECK(vec.capacity() >= 3);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3);
    }

    SUBCASE("emplace_back")
    {
        short_vector<std::pair<int, std::string>, 4> vec;
        vec.emplace_back(42, "hello");
        CHECK(vec.size() == 1);
        CHECK(vec[0].first == 42);
        CHECK(vec[0].second == "hello");
    }

    SUBCASE("emplace_back-returns-reference")
    {
        short_vector<int, 4> vec;
        int& ref = vec.emplace_back(42);
        CHECK(ref == 42);
        ref = 100;
        CHECK(vec[0] == 100);
    }

    SUBCASE("pop_back")
    {
        short_vector<int, 4> vec = {1, 2, 3};
        vec.pop_back();
        CHECK(vec.size() == 2);
        CHECK(vec.back() == 2);
    }

    SUBCASE("pop_back-destroys-element")
    {
        LifetimeTracker::reset_counters();
        {
            short_vector<LifetimeTracker, 4> vec;
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

    SUBCASE("front-and-back")
    {
        short_vector<int, 4> vec = {1, 2, 3};
        CHECK(vec.front() == 1);
        CHECK(vec.back() == 3);

        vec.front() = 10;
        vec.back() = 30;
        CHECK(vec[0] == 10);
        CHECK(vec[2] == 30);
    }

    SUBCASE("const-front-and-back")
    {
        const short_vector<int, 4> vec = {1, 2, 3};
        CHECK(vec.front() == 1);
        CHECK(vec.back() == 3);
    }

    SUBCASE("data-access")
    {
        short_vector<int, 4> vec = {1, 2, 3};
        int* ptr = vec.data();
        CHECK(ptr[0] == 1);
        CHECK(ptr[1] == 2);
        CHECK(ptr[2] == 3);
    }

    SUBCASE("const-data-access")
    {
        const short_vector<int, 4> vec = {1, 2, 3};
        const int* ptr = vec.data();
        CHECK(ptr[0] == 1);
    }

    SUBCASE("clear")
    {
        LifetimeTracker::reset_counters();
        {
            short_vector<LifetimeTracker, 4> vec;
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
        short_vector<int, 8> vec = {1, 2};
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
        short_vector<int, 8> vec = {1, 2};
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
            short_vector<LifetimeTracker, 8> vec;
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
        short_vector<int, 4> vec = {1, 2, 3};
        vec.resize(3);
        CHECK(vec.size() == 3);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3);
    }

    SUBCASE("reserve")
    {
        short_vector<int, 2> vec;
        CHECK(vec.is_inline());

        vec.reserve(10);
        CHECK(!vec.is_inline());
        CHECK(vec.capacity() >= 10);
        CHECK(vec.size() == 0);
    }

    SUBCASE("iterators")
    {
        short_vector<int, 8> vec = {1, 2, 3, 4, 5};

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
        const short_vector<int, 4> vec = {1, 2, 3};
        int sum = 0;
        for (auto it = vec.cbegin(); it != vec.cend(); ++it)
            sum += *it;
        CHECK(sum == 6);
    }

    SUBCASE("reverse-iterators")
    {
        short_vector<int, 8> vec = {1, 2, 3, 4, 5};

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
        const short_vector<int, 4> vec = {1, 2, 3};
        std::vector<int> reversed;
        for (auto it = vec.crbegin(); it != vec.crend(); ++it)
            reversed.push_back(*it);
        CHECK(reversed.size() == 3);
        CHECK(reversed[0] == 3);
        CHECK(reversed[1] == 2);
        CHECK(reversed[2] == 1);
    }

    SUBCASE("reverse-iterators-heap")
    {
        // Test reverse iterators when using heap storage
        short_vector<int, 2> vec = {1, 2, 3, 4, 5};
        CHECK(!vec.is_inline());

        std::vector<int> reversed;
        for (auto it = vec.rbegin(); it != vec.rend(); ++it)
            reversed.push_back(*it);
        CHECK(reversed.size() == 5);
        CHECK(reversed[0] == 5);
        CHECK(reversed[1] == 4);
        CHECK(reversed[2] == 3);
        CHECK(reversed[3] == 2);
        CHECK(reversed[4] == 1);
    }

    SUBCASE("copy-construction")
    {
        LifetimeTracker::reset_counters();
        {
            short_vector<LifetimeTracker, 4> vec1;
            vec1.emplace_back(1);
            vec1.emplace_back(2);
            vec1.emplace_back(3);

            short_vector<LifetimeTracker, 4> vec2(vec1);
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

    SUBCASE("move-construction-inline")
    {
        LifetimeTracker::reset_counters();
        {
            short_vector<LifetimeTracker, 4> vec1;
            vec1.emplace_back(1);
            vec1.emplace_back(2);
            vec1.emplace_back(3);

            short_vector<LifetimeTracker, 4> vec2(std::move(vec1));
            CHECK(vec2.size() == 3);
            CHECK(vec2[0].value == 1);
            CHECK(vec2[1].value == 2);
            CHECK(vec2[2].value == 3);

            // Original should be cleared
            CHECK(vec1.empty());

            CHECK(LifetimeTracker::s_move_count == 3);
        }
    }

    SUBCASE("move-construction-heap")
    {
        short_vector<int, 2> vec1 = {1, 2, 3, 4, 5};
        CHECK(!vec1.is_inline());

        int* original_data = vec1.data();
        short_vector<int, 2> vec2(std::move(vec1));

        CHECK(vec2.size() == 5);
        CHECK(vec2.data() == original_data);
        CHECK(!vec2.is_inline());
        CHECK(vec1.is_inline());
        CHECK(vec1.size() == 0);
    }

    SUBCASE("copy-assignment")
    {
        short_vector<int, 4> vec1 = {1, 2, 3};
        short_vector<int, 4> vec2 = {4, 5};

        vec2 = vec1;
        CHECK(vec2.size() == 3);
        CHECK(vec2[0] == 1);
        CHECK(vec2[1] == 2);
        CHECK(vec2[2] == 3);
    }

    SUBCASE("move-assignment-inline")
    {
        LifetimeTracker::reset_counters();
        {
            short_vector<LifetimeTracker, 4> vec1;
            vec1.emplace_back(1);
            vec1.emplace_back(2);

            short_vector<LifetimeTracker, 4> vec2;
            vec2.emplace_back(10);

            int constructs_before = LifetimeTracker::s_construct_count;
            vec2 = std::move(vec1);

            CHECK(vec2.size() == 2);
            CHECK(vec2[0].value == 1);
            CHECK(vec2[1].value == 2);
            CHECK(vec1.empty());
            // Move assignment should move-construct 2 elements into destination
            CHECK(LifetimeTracker::s_construct_count - constructs_before == 2);
            CHECK(LifetimeTracker::s_move_count >= 2);
        }
        CHECK(LifetimeTracker::s_construct_count == LifetimeTracker::s_destruct_count);
    }

    SUBCASE("move-assignment-heap")
    {
        short_vector<int, 2> vec1 = {1, 2, 3, 4, 5};
        short_vector<int, 2> vec2 = {10, 20, 30, 40};

        int* original_data = vec1.data();
        vec2 = std::move(vec1);

        CHECK(vec2.size() == 5);
        CHECK(vec2.data() == original_data);
        CHECK(vec1.is_inline());
    }

    SUBCASE("initializer-list-assignment")
    {
        short_vector<int, 4> vec = {1, 2, 3};
        vec = {10, 20};

        CHECK(vec.size() == 2);
        CHECK(vec[0] == 10);
        CHECK(vec[1] == 20);
    }

    SUBCASE("erase-single")
    {
        short_vector<int, 8> vec = {1, 2, 3, 4, 5};
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
        short_vector<int, 8> vec = {1, 2, 3, 4, 5};
        auto it = vec.erase(vec.begin() + 1, vec.begin() + 4);
        CHECK(vec.size() == 2);
        CHECK(*it == 5);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 5);
    }

    SUBCASE("erase-range-empty")
    {
        short_vector<int, 4> vec = {1, 2, 3};
        auto it = vec.erase(vec.begin() + 1, vec.begin() + 1);
        CHECK(vec.size() == 3);
        CHECK(*it == 2);
    }

    SUBCASE("erase-with-lifetime-tracking")
    {
        LifetimeTracker::reset_counters();
        {
            short_vector<LifetimeTracker, 4> vec;
            vec.emplace_back(1);
            vec.emplace_back(2);
            vec.emplace_back(3);

            int destructs_before = LifetimeTracker::s_destruct_count;
            vec.erase(vec.begin() + 1);

            CHECK(vec.size() == 2);
            CHECK(vec[0].value == 1);
            CHECK(vec[1].value == 3);
            // Erase should destroy exactly 1 element
            CHECK(LifetimeTracker::s_destruct_count - destructs_before == 1);
        }
        CHECK(LifetimeTracker::s_construct_count == LifetimeTracker::s_destruct_count);
    }

    SUBCASE("insert-lvalue-at-end")
    {
        short_vector<int, 8> vec = {1, 2, 3};
        int value = 4;
        auto it = vec.insert(vec.end(), value);
        CHECK(vec.size() == 4);
        CHECK(*it == 4);
        CHECK(vec[3] == 4);
    }

    SUBCASE("insert-lvalue-at-beginning")
    {
        short_vector<int, 8> vec = {2, 3, 4};
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
        short_vector<int, 8> vec = {1, 2, 4, 5};
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
        short_vector<std::string, 8> vec = {"hello", "world"};
        auto it = vec.insert(vec.begin() + 1, "beautiful");
        CHECK(vec.size() == 3);
        CHECK(*it == "beautiful");
        CHECK(vec[0] == "hello");
        CHECK(vec[1] == "beautiful");
        CHECK(vec[2] == "world");
    }

    SUBCASE("insert-with-lifetime-tracking")
    {
        LifetimeTracker::reset_counters();
        {
            short_vector<LifetimeTracker, 8> vec;
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

    SUBCASE("assign-count-value")
    {
        short_vector<int, 8> vec = {1, 2, 3};
        vec.assign(5, 42);
        CHECK(vec.size() == 5);
        for (size_t i = 0; i < vec.size(); ++i)
            CHECK(vec[i] == 42);
    }

    SUBCASE("assign-iterator-range")
    {
        short_vector<int, 8> vec = {1, 2, 3};
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
        short_vector<int, 4> vec = {1, 2, 3};
        vec.assign({100, 200});
        CHECK(vec.size() == 2);
        CHECK(vec[0] == 100);
        CHECK(vec[1] == 200);
    }

    SUBCASE("swap-same-size")
    {
        short_vector<int, 4> vec1 = {1, 2, 3};
        short_vector<int, 4> vec2 = {4, 5, 6};
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
        short_vector<int, 8> vec1 = {1, 2, 3, 4, 5};
        short_vector<int, 8> vec2 = {10, 20};
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
        short_vector<int, 4> vec1 = {1, 2, 3};
        short_vector<int, 4> vec2;
        vec1.swap(vec2);
        CHECK(vec1.empty());
        CHECK(vec2.size() == 3);
        CHECK(vec2[0] == 1);
        CHECK(vec2[1] == 2);
        CHECK(vec2[2] == 3);
    }

    SUBCASE("swap-self")
    {
        short_vector<int, 4> vec = {1, 2, 3};
        vec.swap(vec);
        CHECK(vec.size() == 3);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3);
    }

    SUBCASE("swap-both-heap")
    {
        short_vector<int, 2> vec1 = {1, 2, 3, 4};
        short_vector<int, 2> vec2 = {10, 20, 30};

        CHECK(!vec1.is_inline());
        CHECK(!vec2.is_inline());

        int* p1 = vec1.data();
        int* p2 = vec2.data();

        vec1.swap(vec2);

        CHECK(vec1.data() == p2);
        CHECK(vec2.data() == p1);
        CHECK(vec1.size() == 3);
        CHECK(vec2.size() == 4);
    }

    SUBCASE("swap-mixed-inline-heap")
    {
        short_vector<int, 4> vec1 = {1, 2};
        short_vector<int, 4> vec2 = {10, 20, 30, 40, 50};

        CHECK(vec1.is_inline());
        CHECK(!vec2.is_inline());

        vec1.swap(vec2);

        CHECK(!vec1.is_inline());
        CHECK(vec1.size() == 5);
        CHECK(vec1[0] == 10);

        CHECK(vec2.is_inline());
        CHECK(vec2.size() == 2);
        CHECK(vec2[0] == 1);
    }

    SUBCASE("swap-with-lifetime-tracking")
    {
        LifetimeTracker::reset_counters();
        {
            short_vector<LifetimeTracker, 4> vec1;
            vec1.emplace_back(1);
            vec1.emplace_back(2);

            short_vector<LifetimeTracker, 4> vec2;
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
        CHECK(LifetimeTracker::s_construct_count == LifetimeTracker::s_destruct_count);
    }

    SUBCASE("equality")
    {
        short_vector<int, 4> vec1 = {1, 2, 3};
        short_vector<int, 4> vec2 = {1, 2, 3};
        short_vector<int, 4> vec3 = {1, 2, 4};
        short_vector<int, 4> vec4 = {1, 2};

        CHECK(vec1 == vec2);
        CHECK(vec1 != vec3);
        CHECK(vec1 != vec4);
    }

    SUBCASE("string-operations")
    {
        short_vector<std::string, 4> vec;
        vec.push_back("hello");
        vec.push_back("world");
        vec.emplace_back(10, 'x');

        CHECK(vec.size() == 3);
        CHECK(vec[0] == "hello");
        CHECK(vec[1] == "world");
        CHECK(vec[2] == "xxxxxxxxxx");

        vec.pop_back();
        CHECK(vec.size() == 2);
        CHECK(vec.back() == "world");

        short_vector<std::string, 4> vec2(vec);
        CHECK(vec2 == vec);

        short_vector<std::string, 4> vec3(std::move(vec));
        CHECK(vec3.size() == 2);
        CHECK(vec.empty());
    }

    SUBCASE("grow-preserves-data")
    {
        short_vector<int, 2> vec = {1, 2};
        CHECK(vec.is_inline());

        for (int i = 3; i <= 10; ++i)
            vec.push_back(i);

        CHECK(!vec.is_inline());
        CHECK(vec.size() == 10);

        for (int i = 0; i < 10; ++i)
            CHECK(vec[i] == i + 1);
    }

    SUBCASE("grow-with-lifetime-tracking")
    {
        LifetimeTracker::reset_counters();
        {
            short_vector<LifetimeTracker, 2> vec;
            vec.emplace_back(1);
            vec.emplace_back(2);
            CHECK(vec.is_inline());

            vec.emplace_back(3);
            CHECK(!vec.is_inline());
            CHECK(vec[0].value == 1);
            CHECK(vec[1].value == 2);
            CHECK(vec[2].value == 3);
        }
        CHECK(LifetimeTracker::s_construct_count == LifetimeTracker::s_destruct_count);
    }

    SUBCASE("lifetime-destruction-order")
    {
        LifetimeTracker::reset_counters();
        {
            short_vector<LifetimeTracker, 4> vec;
            vec.emplace_back(1);
            vec.emplace_back(2);
            vec.emplace_back(3);
            CHECK(LifetimeTracker::s_construct_count == 3);
        }
        CHECK(LifetimeTracker::s_destruct_count == 3);
    }

    SUBCASE("trivial-type-operations")
    {
        short_vector<int, 100> vec;

        for (int i = 0; i < 100; ++i)
            vec.push_back(i);
        CHECK(vec.size() == 100);

        for (int i = 0; i < 100; ++i)
            CHECK(vec[i] == i);

        vec.clear();
        CHECK(vec.empty());

        for (int i = 99; i >= 0; --i)
            vec.push_back(i);

        for (int i = 0; i < 100; ++i)
            CHECK(vec[i] == 99 - i);
    }

    // POD optimization tests - these exercise the memmove/memcpy fast paths
    SUBCASE("pod-erase-first")
    {
        short_vector<int, 8> vec = {1, 2, 3, 4, 5};
        vec.erase(vec.begin());
        CHECK(vec.size() == 4);
        CHECK(vec[0] == 2);
        CHECK(vec[1] == 3);
        CHECK(vec[2] == 4);
        CHECK(vec[3] == 5);
    }

    SUBCASE("pod-erase-last")
    {
        short_vector<int, 8> vec = {1, 2, 3, 4, 5};
        vec.erase(vec.end() - 1);
        CHECK(vec.size() == 4);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 3);
        CHECK(vec[3] == 4);
    }

    SUBCASE("pod-erase-range-all")
    {
        short_vector<int, 8> vec = {1, 2, 3, 4, 5};
        vec.erase(vec.begin(), vec.end());
        CHECK(vec.empty());
    }

    SUBCASE("pod-erase-range-from-start")
    {
        short_vector<int, 8> vec = {1, 2, 3, 4, 5};
        vec.erase(vec.begin(), vec.begin() + 3);
        CHECK(vec.size() == 2);
        CHECK(vec[0] == 4);
        CHECK(vec[1] == 5);
    }

    SUBCASE("pod-erase-range-to-end")
    {
        short_vector<int, 8> vec = {1, 2, 3, 4, 5};
        vec.erase(vec.begin() + 2, vec.end());
        CHECK(vec.size() == 2);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
    }

    SUBCASE("pod-erase-on-heap")
    {
        short_vector<int, 2> vec = {1, 2, 3, 4, 5};
        CHECK(!vec.is_inline());
        vec.erase(vec.begin() + 1, vec.begin() + 4);
        CHECK(vec.size() == 2);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 5);
    }

    SUBCASE("pod-insert-into-empty")
    {
        short_vector<int, 8> vec;
        vec.insert(vec.begin(), 42);
        CHECK(vec.size() == 1);
        CHECK(vec[0] == 42);
    }

    SUBCASE("pod-insert-multiple-at-beginning")
    {
        short_vector<int, 8> vec = {3, 4, 5};
        vec.insert(vec.begin(), 2);
        vec.insert(vec.begin(), 1);
        CHECK(vec.size() == 5);
        for (int i = 0; i < 5; ++i)
            CHECK(vec[i] == i + 1);
    }

    SUBCASE("pod-insert-self-reference")
    {
        // Test inserting an element that references itself (the copy should happen before shift)
        short_vector<int, 8> vec = {1, 2, 3};
        vec.insert(vec.begin() + 1, vec[0]);
        CHECK(vec.size() == 4);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 1);
        CHECK(vec[2] == 2);
        CHECK(vec[3] == 3);
    }

    SUBCASE("pod-insert-triggers-growth")
    {
        short_vector<int, 2> vec = {1, 2};
        CHECK(vec.is_inline());
        vec.insert(vec.begin() + 1, 10);
        CHECK(!vec.is_inline());
        CHECK(vec.size() == 3);
        CHECK(vec.capacity() >= 3);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 10);
        CHECK(vec[2] == 2);
    }

    SUBCASE("pod-insert-on-heap")
    {
        short_vector<int, 2> vec = {1, 2, 3, 4};
        CHECK(!vec.is_inline());
        vec.insert(vec.begin() + 2, 10);
        CHECK(vec.size() == 5);
        CHECK(vec[0] == 1);
        CHECK(vec[1] == 2);
        CHECK(vec[2] == 10);
        CHECK(vec[3] == 3);
        CHECK(vec[4] == 4);
    }

    SUBCASE("pod-copy-construct-large")
    {
        short_vector<int, 4> vec1;
        for (int i = 0; i < 100; ++i)
            vec1.push_back(i);
        CHECK(!vec1.is_inline());

        short_vector<int, 4> vec2(vec1);
        CHECK(vec2.size() == 100);
        for (int i = 0; i < 100; ++i)
            CHECK(vec2[i] == i);
    }

    SUBCASE("pod-move-construct-large")
    {
        short_vector<int, 4> vec1;
        for (int i = 0; i < 100; ++i)
            vec1.push_back(i);
        CHECK(!vec1.is_inline());

        short_vector<int, 4> vec2(std::move(vec1));
        CHECK(vec2.size() == 100);
        for (int i = 0; i < 100; ++i)
            CHECK(vec2[i] == i);
        CHECK(vec1.empty());
    }
}
