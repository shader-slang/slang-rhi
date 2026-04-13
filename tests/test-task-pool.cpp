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

// Basic group lifecycle: create, submit tasks, wait, release.
void testGroupBasic(ITaskPool* pool)
{
    REQUIRE(pool != nullptr);

    static constexpr size_t N = 100;
    static std::atomic<size_t> counter;
    counter = 0;

    auto group = pool->createTaskGroup();

    for (size_t i = 0; i < N; ++i)
    {
        ITaskPool::TaskHandle task = pool->submitTask(
            [](void*)
            {
                counter.fetch_add(1, std::memory_order_relaxed);
            },
            nullptr,
            nullptr,
            nullptr,
            0,
            group
        );
        pool->releaseTask(task);
    }

    pool->waitTaskGroup(group);
    CHECK(counter.load() == N);
    pool->releaseTaskGroup(group);
}

// Sub-tasks spawned from callbacks are tracked by the group.
void testGroupSubTasks(ITaskPool* pool)
{
    REQUIRE(pool != nullptr);

    static std::atomic<size_t> counter;
    counter = 0;

    struct SubTaskPayload
    {
        ITaskPool* pool;
        ITaskPool::TaskGroupHandle group;
        int depth;
    };

    auto group = pool->createTaskGroup();

    auto func = [](void* p)
    {
        SubTaskPayload* payload = static_cast<SubTaskPayload*>(p);
        counter.fetch_add(1, std::memory_order_relaxed);
        if (payload->depth > 0)
        {
            // Spawn two sub-tasks in the same group.
            for (int i = 0; i < 2; ++i)
            {
                SubTaskPayload* sub = new SubTaskPayload{payload->pool, payload->group, payload->depth - 1};
                ITaskPool::TaskHandle task = payload->pool->submitTask(
                    [](void* p2)
                    {
                        SubTaskPayload* sp = static_cast<SubTaskPayload*>(p2);
                        counter.fetch_add(1, std::memory_order_relaxed);
                        if (sp->depth > 0)
                        {
                            for (int j = 0; j < 2; ++j)
                            {
                                SubTaskPayload* sub2 = new SubTaskPayload{sp->pool, sp->group, sp->depth - 1};
                                ITaskPool::TaskHandle t = sp->pool->submitTask(
                                    [](void* p3)
                                    {
                                        SubTaskPayload* sp2 = static_cast<SubTaskPayload*>(p3);
                                        counter.fetch_add(1, std::memory_order_relaxed);
                                        SLANG_UNUSED(sp2);
                                    },
                                    sub2,
                                    [](void* p3)
                                    {
                                        delete static_cast<SubTaskPayload*>(p3);
                                    },
                                    nullptr,
                                    0,
                                    sp->group
                                );
                                sp->pool->releaseTask(t);
                            }
                        }
                    },
                    sub,
                    [](void* p2)
                    {
                        delete static_cast<SubTaskPayload*>(p2);
                    },
                    nullptr,
                    0,
                    payload->group
                );
                payload->pool->releaseTask(task);
            }
        }
    };

    SubTaskPayload* payload = new SubTaskPayload{pool, group, 2};
    ITaskPool::TaskHandle task = pool->submitTask(
        func,
        payload,
        [](void* p)
        {
            delete static_cast<SubTaskPayload*>(p);
        },
        nullptr,
        0,
        group
    );
    pool->releaseTask(task);

    pool->waitTaskGroup(group);
    // 1 root (depth 2) + 2 children (depth 1) + 4 leaves (depth 0) = 7
    CHECK(counter.load() == 7);
    pool->releaseTaskGroup(group);
}

// Empty group: wait immediately after create.
void testGroupEmpty(ITaskPool* pool)
{
    REQUIRE(pool != nullptr);

    auto group = pool->createTaskGroup();
    pool->waitTaskGroup(group);
    pool->releaseTaskGroup(group);
}

// Group with dependencies: tasks have both a group and dependency handles.
void testGroupWithDependencies(ITaskPool* pool)
{
    REQUIRE(pool != nullptr);

    static std::atomic<size_t> order;
    order = 0;

    auto group = pool->createTaskGroup();

    // First task in the group.
    static size_t firstOrder;
    ITaskPool::TaskHandle first = pool->submitTask(
        [](void*)
        {
            firstOrder = order.fetch_add(1, std::memory_order_relaxed);
        },
        nullptr,
        nullptr,
        nullptr,
        0,
        group
    );

    // Second task depends on first, also in the group.
    static size_t secondOrder;
    ITaskPool::TaskHandle second = pool->submitTask(
        [](void*)
        {
            secondOrder = order.fetch_add(1, std::memory_order_relaxed);
        },
        nullptr,
        nullptr,
        &first,
        1,
        group
    );

    pool->releaseTask(first);
    pool->releaseTask(second);

    pool->waitTaskGroup(group);
    CHECK(firstOrder < secondOrder);
    CHECK(order.load() == 2);
    pool->releaseTaskGroup(group);
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
    SUBCASE("group-basic")
    {
        testGroupBasic(pool);
    }
    SUBCASE("group-sub-tasks")
    {
        testGroupSubTasks(pool);
    }
    SUBCASE("group-empty")
    {
        testGroupEmpty(pool);
    }
    SUBCASE("group-with-dependencies")
    {
        testGroupWithDependencies(pool);
    }
}

TEST_CASE("task-pool-threaded")
{
    ComPtr<ITaskPool> pool(new ThreadedTaskPool());

    SUBCASE("simple")
    {
        for (int i = 0; i < 100; ++i)
        {
            testSimple(pool);
        }
    }
    SUBCASE("wait-all")
    {
        for (int i = 0; i < 100; ++i)
        {
            testWaitAll(pool);
        }
    }
    SUBCASE("simple-dependency")
    {
        for (int i = 0; i < 100; ++i)
        {
            testSimpleDependency(pool);
        }
    }
    SUBCASE("recursive-dependency")
    {
        for (int i = 0; i < 100; ++i)
        {
            testRecursiveDependency(pool);
        }
    }
    SUBCASE("fibonacci")
    {
        testFibonacci(pool);
    }
    SUBCASE("group-basic")
    {
        for (int i = 0; i < 100; ++i)
        {
            testGroupBasic(pool);
        }
    }
    SUBCASE("group-sub-tasks")
    {
        for (int i = 0; i < 100; ++i)
        {
            testGroupSubTasks(pool);
        }
    }
    SUBCASE("group-empty")
    {
        for (int i = 0; i < 100; ++i)
        {
            testGroupEmpty(pool);
        }
    }
    SUBCASE("group-with-dependencies")
    {
        for (int i = 0; i < 100; ++i)
        {
            testGroupWithDependencies(pool);
        }
    }
}

// Work-stealing tests: use a single worker thread to force scenarios that
// would deadlock without work-stealing in wait functions.

// A task callback calls waitTask on another task. With 1 worker thread and
// no work-stealing, the worker blocks and the waited-on task never runs.
void testWorkStealingWaitTaskFromCallback(ITaskPool* pool)
{
    REQUIRE(pool != nullptr);

    std::atomic<int> result{0};

    // Task A: sets result to 1.
    auto taskA = pool->submitTask(
        [](void* p)
        {
            static_cast<std::atomic<int>*>(p)->store(1, std::memory_order_relaxed);
        },
        &result,
        nullptr,
        nullptr,
        0
    );

    // Task B: waits on A from inside its callback, then sets result to 2.
    struct Payload
    {
        ITaskPool* pool;
        ITaskPool::TaskHandle taskA;
        std::atomic<int>* result;
    };
    Payload payload{pool, taskA, &result};

    auto taskB = pool->submitTask(
        [](void* p)
        {
            auto* ctx = static_cast<Payload*>(p);
            ctx->pool->waitTask(ctx->taskA);
            CHECK(ctx->result->load(std::memory_order_relaxed) == 1);
            ctx->result->store(2, std::memory_order_relaxed);
        },
        &payload,
        nullptr,
        nullptr,
        0
    );

    pool->waitTask(taskB);
    CHECK(result.load() == 2);

    pool->releaseTask(taskA);
    pool->releaseTask(taskB);
}

// Nested wait chain: task C waits on B, B waits on A. With 1 worker thread,
// this requires two levels of work-stealing to avoid deadlock.
void testWorkStealingNestedWait(ITaskPool* pool)
{
    REQUIRE(pool != nullptr);

    std::atomic<int> order{0};

    auto taskA = pool->submitTask(
        [](void* p)
        {
            static_cast<std::atomic<int>*>(p)->fetch_add(1, std::memory_order_relaxed);
        },
        &order,
        nullptr,
        nullptr,
        0
    );

    struct WaitPayload
    {
        ITaskPool* pool;
        ITaskPool::TaskHandle dep;
        std::atomic<int>* order;
    };
    WaitPayload payloadB{pool, taskA, &order};

    auto taskB = pool->submitTask(
        [](void* p)
        {
            auto* ctx = static_cast<WaitPayload*>(p);
            ctx->pool->waitTask(ctx->dep);
            ctx->order->fetch_add(1, std::memory_order_relaxed);
        },
        &payloadB,
        nullptr,
        nullptr,
        0
    );

    WaitPayload payloadC{pool, taskB, &order};

    auto taskC = pool->submitTask(
        [](void* p)
        {
            auto* ctx = static_cast<WaitPayload*>(p);
            ctx->pool->waitTask(ctx->dep);
            ctx->order->fetch_add(1, std::memory_order_relaxed);
        },
        &payloadC,
        nullptr,
        nullptr,
        0
    );

    pool->waitTask(taskC);
    CHECK(order.load() == 3);

    pool->releaseTask(taskA);
    pool->releaseTask(taskB);
    pool->releaseTask(taskC);
}

// A task callback uses waitTaskGroup to wait on sub-tasks it spawns.
// With 1 worker, the callback's thread must steal sub-tasks to make progress.
void testWorkStealingWaitGroupFromCallback(ITaskPool* pool)
{
    REQUIRE(pool != nullptr);

    std::atomic<int> sum{0};

    struct Payload
    {
        ITaskPool* pool;
        std::atomic<int>* sum;
    };
    Payload payload{pool, &sum};

    auto task = pool->submitTask(
        [](void* p)
        {
            auto* ctx = static_cast<Payload*>(p);
            auto group = ctx->pool->createTaskGroup();

            static constexpr int N = 10;
            ITaskPool::TaskHandle subtasks[N];
            for (int i = 0; i < N; ++i)
            {
                subtasks[i] = ctx->pool->submitTask(
                    [](void* p2)
                    {
                        static_cast<std::atomic<int>*>(p2)->fetch_add(1, std::memory_order_relaxed);
                    },
                    ctx->sum,
                    nullptr,
                    nullptr,
                    0,
                    group
                );
            }

            ctx->pool->waitTaskGroup(group);
            ctx->pool->releaseTaskGroup(group);

            for (int i = 0; i < N; ++i)
                ctx->pool->releaseTask(subtasks[i]);
        },
        &payload,
        nullptr,
        nullptr,
        0
    );

    pool->waitTask(task);
    CHECK(sum.load() == 10);
    pool->releaseTask(task);
}

TEST_CASE("task-pool-work-stealing")
{
    // Use a single worker thread to force work-stealing in wait functions.
    // Without work-stealing, these tests would deadlock.
    ComPtr<ITaskPool> pool(new ThreadedTaskPool(1));

    SUBCASE("wait-task-from-callback")
    {
        for (int i = 0; i < 100; ++i)
        {
            testWorkStealingWaitTaskFromCallback(pool);
        }
    }
    SUBCASE("nested-wait")
    {
        for (int i = 0; i < 100; ++i)
        {
            testWorkStealingNestedWait(pool);
        }
    }
    SUBCASE("wait-group-from-callback")
    {
        for (int i = 0; i < 100; ++i)
        {
            testWorkStealingWaitGroupFromCallback(pool);
        }
    }
}
