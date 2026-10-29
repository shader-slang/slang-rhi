#include "testing.h"

#include "core/task-pool.h"

#include <thread>

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
            }
        );
    }

    for (size_t i = 0; i < N; ++i)
    {
        CAPTURE(i);
        pool->waitAndReleaseTask(tasks[i]);
        CHECK(result[i] == (size_t)i);
        CHECK(deleted[i]);
    }
}

// Basic group lifecycle: create, submit tasks, wait, release.
void testGroupBasic(ITaskPool* pool)
{
    REQUIRE(pool != nullptr);

    static constexpr size_t N = 100;
    static std::atomic<size_t> counter;
    static std::atomic<size_t> deleted;
    counter = 0;
    deleted = 0;

    auto group = pool->createTaskGroup();

    for (size_t i = 0; i < N; ++i)
    {
        ITaskPool::TaskHandle task = pool->submitTask(
            [](void*)
            {
                counter.fetch_add(1, std::memory_order_relaxed);
            },
            nullptr,
            [](void*)
            {
                deleted.fetch_add(1, std::memory_order_relaxed);
            },
            group
        );
        pool->releaseTask(task);
    }

    pool->waitAndReleaseTaskGroup(group);
    CHECK(counter.load() == N);
    CHECK(deleted.load() == N);
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
        group
    );
    pool->releaseTask(task);

    pool->waitAndReleaseTaskGroup(group);
    // 1 root (depth 2) + 2 children (depth 1) + 4 leaves (depth 0) = 7
    CHECK(counter.load() == 7);
}

// Empty group: wait immediately after create.
void testGroupEmpty(ITaskPool* pool)
{
    REQUIRE(pool != nullptr);

    auto group = pool->createTaskGroup();
    pool->waitAndReleaseTaskGroup(group);
}

void testTaskPool(ITaskPool* pool, int iterations)
{
    SUBCASE("simple")
    {
        for (int i = 0; i < iterations; ++i)
        {
            testSimple(pool);
        }
    }
    SUBCASE("group-basic")
    {
        for (int i = 0; i < iterations; ++i)
        {
            testGroupBasic(pool);
        }
    }
    SUBCASE("group-sub-tasks")
    {
        for (int i = 0; i < iterations; ++i)
        {
            testGroupSubTasks(pool);
        }
    }
    SUBCASE("group-empty")
    {
        for (int i = 0; i < iterations; ++i)
        {
            testGroupEmpty(pool);
        }
    }
}

TEST_CASE("task-pool-blocking")
{
    ComPtr<ITaskPool> pool(new BlockingTaskPool());
    testTaskPool(pool, 1);
}

TEST_CASE("task-pool-threaded")
{
    ComPtr<ITaskPool> pool(new ThreadedTaskPool());
    testTaskPool(pool, 10);
}

TEST_CASE("task-pool-threaded-single-worker")
{
    ComPtr<ITaskPool> pool(new ThreadedTaskPool(1));
    testTaskPool(pool, 1);
}

// Work-stealing tests use a single worker thread so the waiting caller also
// participates in task execution.

void testExternalWaitStealsReadyTask(ITaskPool* pool)
{
    REQUIRE(pool != nullptr);

    std::atomic<bool> blockerStarted{false};
    std::atomic<bool> releaseBlocker{false};
    std::atomic<bool> targetExecuted{false};
    struct BlockerState
    {
        std::atomic<bool>* started;
        std::atomic<bool>* release;
    } blockerState{&blockerStarted, &releaseBlocker};

    auto blocker = pool->submitTask(
        [](void* data)
        {
            auto* state = static_cast<BlockerState*>(data);
            state->started->store(true, std::memory_order_release);
            while (!state->release->load(std::memory_order_acquire))
                std::this_thread::yield();
        },
        &blockerState,
        nullptr
    );

    while (!blockerStarted.load(std::memory_order_acquire))
        std::this_thread::yield();

    auto target = pool->submitTask(
        [](void* data)
        {
            static_cast<std::atomic<bool>*>(data)->store(true, std::memory_order_relaxed);
        },
        &targetExecuted,
        nullptr
    );

    // The only worker is blocked, so the waiting thread must execute the target here.
    pool->waitAndReleaseTask(target);
    CHECK(targetExecuted.load(std::memory_order_relaxed));

    releaseBlocker.store(true, std::memory_order_release);
    pool->waitAndReleaseTask(blocker);
}

// A task callback waits on another task that was submitted first.
// The earlier task can therefore make progress on another executor.
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
        nullptr
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
            ctx->pool->waitAndReleaseTask(ctx->taskA);
            CHECK(ctx->result->load(std::memory_order_relaxed) == 1);
            ctx->result->store(2, std::memory_order_relaxed);
        },
        &payload,
        nullptr
    );

    pool->waitAndReleaseTask(taskB);
    CHECK(result.load() == 2);
}

// Nested wait chain: task C waits on B, B waits on A. Each waited-on task was
// submitted first and can therefore make progress on another executor.
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
        nullptr
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
            ctx->pool->waitAndReleaseTask(ctx->dep);
            ctx->order->fetch_add(1, std::memory_order_relaxed);
        },
        &payloadB,
        nullptr
    );

    WaitPayload payloadC{pool, taskB, &order};

    auto taskC = pool->submitTask(
        [](void* p)
        {
            auto* ctx = static_cast<WaitPayload*>(p);
            ctx->pool->waitAndReleaseTask(ctx->dep);
            ctx->order->fetch_add(1, std::memory_order_relaxed);
        },
        &payloadC,
        nullptr
    );

    pool->waitAndReleaseTask(taskC);
    CHECK(order.load() == 3);
}

// A task callback waits on a group of sub-tasks it spawns.
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
                    group
                );
            }

            ctx->pool->waitAndReleaseTaskGroup(group);

            for (int i = 0; i < N; ++i)
                ctx->pool->releaseTask(subtasks[i]);
        },
        &payload,
        nullptr
    );

    pool->waitAndReleaseTask(task);
    CHECK(sum.load() == 10);
}

struct NestedGroupState
{
    std::atomic<int> executed{0};
};

static thread_local NestedGroupState* tls_expectedNestedGroupState = nullptr;

struct NestedGroupTaskPayload
{
    ITaskPool* pool;
    ITaskPool::TaskGroupHandle group;
    NestedGroupState* state;
    std::atomic<int>* crossGroupExecutions;
    int depth;
};

void runNestedGroupTask(void* data)
{
    auto* payload = static_cast<NestedGroupTaskPayload*>(data);
    payload->state->executed.fetch_add(1, std::memory_order_relaxed);
    if (tls_expectedNestedGroupState && tls_expectedNestedGroupState != payload->state)
        payload->crossGroupExecutions->fetch_add(1, std::memory_order_relaxed);

    if (payload->depth == 0)
        return;

    for (int i = 0; i < 2; ++i)
    {
        auto* child = new NestedGroupTaskPayload{
            payload->pool,
            payload->group,
            payload->state,
            payload->crossGroupExecutions,
            payload->depth - 1,
        };
        auto task = payload->pool->submitTask(
            runNestedGroupTask,
            child,
            [](void* childData)
            {
                delete static_cast<NestedGroupTaskPayload*>(childData);
            },
            payload->group
        );
        payload->pool->releaseTask(task);
    }
}

// Saturate a single-worker pool with two callbacks that each wait on a
// dynamically growing task group. The thread waiting on a task becomes the
// second executor, so both executors are inside callbacks when the group work
// is queued. Nested group-specific stealing is required to make progress.
void testNestedGroupWaitWithSaturatedWorkers()
{
    ComPtr<ITaskPool> pool(new ThreadedTaskPool(1));
    std::atomic<int> parentsStarted{0};
    std::atomic<int> submissionTurn{1};
    std::atomic<int> crossGroupExecutions{0};
    NestedGroupState groupStates[2];

    struct ParentPayload
    {
        ITaskPool* pool;
        std::atomic<int>* parentsStarted;
        std::atomic<int>* submissionTurn;
        int index;
        NestedGroupState* groupState;
        NestedGroupState* firstGroupState;
        std::atomic<int>* crossGroupExecutions;
    };
    ParentPayload payloads[] = {
        {pool, &parentsStarted, &submissionTurn, 0, &groupStates[0], &groupStates[0], &crossGroupExecutions},
        {pool, &parentsStarted, &submissionTurn, 1, &groupStates[1], &groupStates[0], &crossGroupExecutions},
    };

    ITaskPool::TaskHandle parents[2];
    for (size_t i = 0; i < std::size(parents); ++i)
    {
        parents[i] = pool->submitTask(
            [](void* data)
            {
                auto* payload = static_cast<ParentPayload*>(data);
                payload->parentsStarted->fetch_add(1, std::memory_order_release);
                while (payload->parentsStarted->load(std::memory_order_acquire) != 2)
                    std::this_thread::yield();

                NestedGroupState* previousExpectedState = tls_expectedNestedGroupState;
                tls_expectedNestedGroupState = payload->groupState;

                auto group = payload->pool->createTaskGroup();
                // Queue group 1 first, then let group 0 enter its nested wait first. An
                // unfiltered nested wait will deterministically execute the wrong root task.
                while (payload->submissionTurn->load(std::memory_order_acquire) != payload->index)
                    std::this_thread::yield();
                auto* root = new NestedGroupTaskPayload{
                    payload->pool,
                    group,
                    payload->groupState,
                    payload->crossGroupExecutions,
                    4,
                };
                auto rootTask = payload->pool->submitTask(
                    runNestedGroupTask,
                    root,
                    [](void* rootData)
                    {
                        delete static_cast<NestedGroupTaskPayload*>(rootData);
                    },
                    group
                );
                payload->pool->releaseTask(rootTask);

                if (payload->index == 1)
                {
                    payload->submissionTurn->store(0, std::memory_order_release);
                    while (payload->firstGroupState->executed.load(std::memory_order_acquire) == 0)
                        std::this_thread::yield();
                }

                payload->pool->waitAndReleaseTaskGroup(group);
                tls_expectedNestedGroupState = previousExpectedState;
            },
            &payloads[i],
            nullptr
        );
    }

    for (auto parent : parents)
        pool->waitAndReleaseTask(parent);

    // Each group contains a complete binary tree with depth 4: 2^5 - 1 tasks.
    CHECK(groupStates[0].executed.load(std::memory_order_relaxed) == 31);
    CHECK(groupStates[1].executed.load(std::memory_order_relaxed) == 31);
    CHECK(crossGroupExecutions.load(std::memory_order_relaxed) == 0);
}

TEST_CASE("task-pool-work-stealing")
{
    // Use a single worker thread so the waiting caller participates in execution.
    ComPtr<ITaskPool> pool(new ThreadedTaskPool(1));

    SUBCASE("external-wait-steals-ready-task")
    {
        testExternalWaitStealsReadyTask(pool);
    }

    SUBCASE("wait-task-from-callback")
    {
        for (int i = 0; i < 10000; ++i)
        {
            testWorkStealingWaitTaskFromCallback(pool);
        }
    }
    SUBCASE("nested-wait")
    {
        for (int i = 0; i < 10000; ++i)
        {
            testWorkStealingNestedWait(pool);
        }
    }
    SUBCASE("wait-group-from-callback")
    {
        for (int i = 0; i < 10000; ++i)
        {
            testWorkStealingWaitGroupFromCallback(pool);
        }
    }
}

TEST_CASE("task-pool-nested-group-wait-saturated-workers")
{
    testNestedGroupWaitWithSaturatedWorkers();
}
