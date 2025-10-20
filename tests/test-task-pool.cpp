#include "testing.h"

#include "core/task-pool.h"

#include <thread>
#include <string>
#include <functional>

using namespace rhi;

// Create a number of tasks and wait for each of them individually.
void testSimple(ITaskPool* pool)
{
    REQUIRE(pool != nullptr);

    static constexpr size_t N = 1000;
    static size_t result[N];
    static bool deleted[N];
    ITaskPool::TaskHandle tasks[N];

    ::memset(result, 0, sizeof(result));
    ::memset(deleted, 0, sizeof(deleted));

    for (size_t i = 0; i < N; ++i)
    {
        size_t* payload = new size_t{i};
        tasks[i] = pool->submitTask(
            [](void* payload)
            {
                size_t j = *static_cast<size_t*>(payload);
                result[j] = j;
            },
            payload,
            [](void* payload)
            {
                size_t j = *static_cast<size_t*>(payload);
                deleted[j] = true;
                delete static_cast<size_t*>(payload);
            },
            nullptr,
            0
        );
    }

    for (size_t i = 0; i < N; ++i)
    {
        CAPTURE(i);
        CHECK(!deleted[i]);
        pool->waitTask(tasks[i]);
        pool->releaseTask(tasks[i]);
        CHECK(result[i] == (size_t)i);
    }

    pool->waitAll();

    for (size_t i = 0; i < N; ++i)
    {
        CAPTURE(i);
        CHECK(deleted[i]);
    }
}

// Create a number of tasks and wait for all of them at once.
void testWaitAll(ITaskPool* pool)
{
    REQUIRE(pool != nullptr);

    static constexpr size_t N = 1000;
    static size_t result[N];
    static bool deleted[N];

    ::memset(result, 0, sizeof(result));
    ::memset(deleted, 0, sizeof(deleted));

    for (size_t i = 0; i < N; ++i)
    {
        size_t* payload = new size_t{i};
        ITaskPool::TaskHandle task = pool->submitTask(
            [](void* payload)
            {
                size_t j = *static_cast<size_t*>(payload);
                result[j] = j;
            },
            payload,
            [](void* payload)
            {
                size_t j = *static_cast<size_t*>(payload);
                deleted[j] = true;
                delete static_cast<size_t*>(payload);
            },
            nullptr,
            0
        );
        CHECK(!deleted[i]);
        pool->releaseTask(task);
    }

    pool->waitAll();

    for (size_t i = 0; i < N; ++i)
    {
        CAPTURE(i);
        CHECK(result[i] == (size_t)i);
        CHECK(deleted[i]);
    }
}

// Create a number of tasks and wait for all of them at once.
void testSimpleDependency(ITaskPool* pool)
{
    REQUIRE(pool != nullptr);

    static constexpr size_t N = 1000;
    static size_t result[N];
    static ITaskPool::TaskHandle tasks[N];
    static std::atomic<size_t> finished;

    finished = 0;

    for (size_t i = 0; i < N; ++i)
    {
        tasks[i] = pool->submitTask(
            [](void* payload)
            {
                size_t j = (size_t)(uintptr_t)payload;
                result[j] = j;
                finished++;
            },
            (void*)i,
            nullptr,
            nullptr,
            0
        );
    }

    ITaskPool::TaskHandle waitTask = pool->submitTask(
        [](void*)
        {
            CHECK(finished == N);
        },
        nullptr,
        nullptr,
        tasks,
        N
    );

    for (size_t i = 0; i < N; ++i)
    {
        pool->releaseTask(tasks[i]);
    }

    pool->waitTask(waitTask);
    pool->releaseTask(waitTask);

    for (size_t i = 0; i < N; ++i)
    {
        CAPTURE(i);
        CHECK(result[i] == (size_t)i);
    }
}

inline ITaskPool::TaskHandle spawn(ITaskPool* pool, int depth)
{
    if (depth > 0)
    {
        ITaskPool::TaskHandle a = spawn(pool, depth - 1);
        ITaskPool::TaskHandle b = spawn(pool, depth - 1);
        ITaskPool::TaskHandle tasks[] = {a, b};
        ITaskPool::TaskHandle c = pool->submitTask(
            [](void*)
            {
            },
            nullptr,
            nullptr,
            tasks,
            2
        );
        pool->releaseTask(a);
        pool->releaseTask(b);
        return c;
    }
    else
    {
        return pool->submitTask(
            [](void*)
            {
            },
            nullptr,
            nullptr,
            nullptr,
            0
        );
    }
}

void testRecursiveDependency(ITaskPool* pool)
{
    REQUIRE(pool != nullptr);

    ITaskPool::TaskHandle task = spawn(pool, 10);
    pool->waitTask(task);
    pool->releaseTask(task);
}

struct FibonacciPayload
{
    int result;
    ITaskPool::TaskHandle a;
    ITaskPool::TaskHandle b;
};

static ITaskPool* fibonacciPool;

inline ITaskPool::TaskHandle fibonacciTask(int n)
{
    FibonacciPayload* payload = new FibonacciPayload{};

    if (n <= 1)
    {
        payload->result = n;
        payload->a = nullptr;
        payload->b = nullptr;
        return fibonacciPool->submitTask(
            [](void* payload)
            {
            },
            payload,
            ::free,
            nullptr,
            0
        );
    }
    else
    {
        payload->a = fibonacciTask(n - 1);
        payload->b = fibonacciTask(n - 2);
        ITaskPool::TaskHandle tasks[] = {payload->a, payload->b};
        return fibonacciPool->submitTask(
            [](void* payload)
            {
                FibonacciPayload* p = static_cast<FibonacciPayload*>(payload);
                FibonacciPayload* pa = static_cast<FibonacciPayload*>(fibonacciPool->getTaskPayload(p->a));
                FibonacciPayload* pb = static_cast<FibonacciPayload*>(fibonacciPool->getTaskPayload(p->b));
                p->result = pa->result + pb->result;
                fibonacciPool->releaseTask(p->a);
                fibonacciPool->releaseTask(p->b);
            },
            payload,
            ::free,
            tasks,
            2
        );
    }
}

inline int fibonacci(int n)
{
    return n <= 1 ? n : fibonacci(n - 1) + fibonacci(n - 2);
}

void testFibonacci(ITaskPool* pool)
{
    REQUIRE(pool != nullptr);

    fibonacciPool = pool;
    int N = 25;
    int expected = fibonacci(N);
    ITaskPool::TaskHandle task = fibonacciTask(N);
    pool->waitTask(task);
    int result = static_cast<FibonacciPayload*>(pool->getTaskPayload(task))->result;
    CHECK(result == expected);
    pool->releaseTask(task);
}

TEST_CASE("task-pool-blocking")
{
    ComPtr<ITaskPool> pool(new BlockingTaskPool());

    SUBCASE("simple")
    {
        testSimple(pool);
    }
    SUBCASE("wait-all")
    {
        testWaitAll(pool);
    }
    SUBCASE("simple-dependency")
    {
        testSimpleDependency(pool);
    }
    SUBCASE("recursive-dependency")
    {
        testRecursiveDependency(pool);
    }
    SUBCASE("fibonacci")
    {
        testFibonacci(pool);
    }
}
