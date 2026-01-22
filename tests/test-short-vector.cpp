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
    static int s_constructCount;
    static int s_destructCount;
    static int s_copyCount;
    static int s_moveCount;

    int value;

    static void resetCounters()
    {
        s_constructCount = 0;
        s_destructCount = 0;
        s_copyCount = 0;
        s_moveCount = 0;
    }

    LifetimeTracker()
        : value(0)
    {
        ++s_constructCount;
    }

    explicit LifetimeTracker(int v)
        : value(v)
    {
        ++s_constructCount;
    }

    LifetimeTracker(const LifetimeTracker& other)
        : value(other.value)
    {
        ++s_constructCount;
        ++s_copyCount;
    }

    LifetimeTracker(LifetimeTracker&& other) noexcept
        : value(other.value)
    {
        ++s_constructCount;
        ++s_moveCount;
        other.value = -1;
    }

    LifetimeTracker& operator=(const LifetimeTracker& other)
    {
        value = other.value;
        ++s_copyCount;
        return *this;
    }

    LifetimeTracker& operator=(LifetimeTracker&& other) noexcept
    {
        value = other.value;
        ++s_moveCount;
        other.value = -1;
        return *this;
    }

    ~LifetimeTracker() { ++s_destructCount; }
};

int LifetimeTracker::s_constructCount = 0;
int LifetimeTracker::s_destructCount = 0;
int LifetimeTracker::s_copyCount = 0;
int LifetimeTracker::s_moveCount = 0;

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
        LifetimeTracker::resetCounters();
        {
            short_vector<LifetimeTracker, 4> vec;
            vec.emplace_back(1);
            vec.emplace_back(2);
            CHECK(LifetimeTracker::s_destructCount == 0);

            vec.pop_back();
            CHECK(LifetimeTracker::s_destructCount == 1);
            CHECK(vec.size() == 1);
            CHECK(vec[0].value == 1);
        }
        CHECK(LifetimeTracker::s_destructCount == 2);
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
        LifetimeTracker::resetCounters();
        {
            short_vector<LifetimeTracker, 4> vec;
            vec.emplace_back(1);
            vec.emplace_back(2);
            vec.emplace_back(3);
            CHECK(vec.size() == 3);
            CHECK(LifetimeTracker::s_constructCount == 3);
            CHECK(LifetimeTracker::s_destructCount == 0);

            vec.clear();
            CHECK(vec.empty());
            CHECK(LifetimeTracker::s_destructCount == 3);
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
        LifetimeTracker::resetCounters();
        {
            short_vector<LifetimeTracker, 8> vec;
            vec.emplace_back(1);
            vec.emplace_back(2);
            vec.emplace_back(3);
            vec.emplace_back(4);
            vec.emplace_back(5);
            CHECK(LifetimeTracker::s_destructCount == 0);

            vec.resize(2);
            CHECK(vec.size() == 2);
            CHECK(vec[0].value == 1);
            CHECK(vec[1].value == 2);
            CHECK(LifetimeTracker::s_destructCount == 3);
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

    SUBCASE("copy-construction")
    {
        LifetimeTracker::resetCounters();
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

            CHECK(LifetimeTracker::s_copyCount == 3);
        }
    }

    SUBCASE("move-construction-inline")
    {
        LifetimeTracker::resetCounters();
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

            CHECK(LifetimeTracker::s_moveCount == 3);
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
        LifetimeTracker::resetCounters();
        {
            short_vector<LifetimeTracker, 4> vec1;
            vec1.emplace_back(1);
            vec1.emplace_back(2);

            short_vector<LifetimeTracker, 4> vec2;
            vec2.emplace_back(10);

            int constructs_before = LifetimeTracker::s_constructCount;
            vec2 = std::move(vec1);

            CHECK(vec2.size() == 2);
            CHECK(vec2[0].value == 1);
            CHECK(vec2[1].value == 2);
            CHECK(vec1.empty());
            // Move assignment should move-construct 2 elements into destination
            CHECK(LifetimeTracker::s_constructCount - constructs_before == 2);
            CHECK(LifetimeTracker::s_moveCount >= 2);
        }
        CHECK(LifetimeTracker::s_constructCount == LifetimeTracker::s_destructCount);
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
        LifetimeTracker::resetCounters();
        {
            short_vector<LifetimeTracker, 4> vec;
            vec.emplace_back(1);
            vec.emplace_back(2);
            vec.emplace_back(3);

            int destructs_before = LifetimeTracker::s_destructCount;
            vec.erase(vec.begin() + 1);

            CHECK(vec.size() == 2);
            CHECK(vec[0].value == 1);
            CHECK(vec[1].value == 3);
            // Erase should destroy exactly 1 element
            CHECK(LifetimeTracker::s_destructCount - destructs_before == 1);
        }
        CHECK(LifetimeTracker::s_constructCount == LifetimeTracker::s_destructCount);
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
        LifetimeTracker::resetCounters();
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
        CHECK(LifetimeTracker::s_constructCount == LifetimeTracker::s_destructCount);
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
        LifetimeTracker::resetCounters();
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
        CHECK(LifetimeTracker::s_constructCount == LifetimeTracker::s_destructCount);
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
        LifetimeTracker::resetCounters();
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
        CHECK(LifetimeTracker::s_constructCount == LifetimeTracker::s_destructCount);
    }

    SUBCASE("lifetime-destruction-order")
    {
        LifetimeTracker::resetCounters();
        {
            short_vector<LifetimeTracker, 4> vec;
            vec.emplace_back(1);
            vec.emplace_back(2);
            vec.emplace_back(3);
            CHECK(LifetimeTracker::s_constructCount == 3);
        }
        CHECK(LifetimeTracker::s_destructCount == 3);
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
}
