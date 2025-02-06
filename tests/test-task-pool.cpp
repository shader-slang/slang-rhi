#include "testing.h"

#include "core/task-pool.h"

#include <thread>
#include <string>
#include <functional>

using namespace rhi;

class SimpleTask : public Task
{
public:
    SimpleTask(
        std::function<void()> onCreate = nullptr,
        std::function<void()> onDestroy = nullptr,
        std::function<void()> onRun = nullptr
    )
        : m_onCreate(onCreate)
        , m_onDestroy(onDestroy)
        , m_onRun(onRun)
    {
        if (onCreate)
            onCreate();
    }

    ~SimpleTask()
    {
        if (m_onDestroy)
            m_onDestroy();
    }

    void run() override
    {
        if (m_onRun)
            m_onRun();
    }

private:
    std::function<void()> m_onCreate;
    std::function<void()> m_onDestroy;
    std::function<void()> m_onRun;
};

TEST_CASE("task-pool")
{
    TaskPool pool;

    SUBCASE("wait single")
    {
        std::atomic<bool> alive(false);
        std::atomic<bool> done(false);
        RefPtr<SimpleTask> task =
            new SimpleTask([&]() { alive = true; }, [&]() { alive = false; }, [&]() { done = true; });
        REQUIRE(task);
        REQUIRE(alive);
        REQUIRE(!done);
        TaskHandle taskHandle = pool.submitTask(task);
        REQUIRE(taskHandle);
        task.setNull();
        pool.waitForCompletion(taskHandle);
        CHECK(done);
        pool.releaseTask(taskHandle);
        CHECK(!alive);
    }

    SUBCASE("wait multiple")
    {
        static constexpr int N = 100;
        std::atomic<bool> alive[N];
        std::atomic<bool> done[N];
        TaskHandle taskHandles[N];
        for (int i = 0; i < N; ++i)
        {
            alive[i] = false;
            done[i] = false;
            RefPtr<SimpleTask> task = new SimpleTask(
                [&, i]() { alive[i] = true; },
                [&, i]() { alive[i] = false; },
                [&, i]() { done[i] = true; }
            );
            taskHandles[i] = pool.submitTask(task);
        }
        pool.waitForCompletion(taskHandles, N);
        for (int i = 0; i < N; ++i)
        {
            pool.releaseTask(taskHandles[i]);
            CHECK(!alive[i]);
            CHECK(done[i]);
        }
    }

    SUBCASE("simple dependency")
    {
        std::atomic<bool> aliveA(false);
        std::atomic<bool> doneA(false);
        std::atomic<bool> aliveB(false);
        std::atomic<bool> doneB(false);
        RefPtr<SimpleTask> taskA =
            new SimpleTask([&]() { aliveA = true; }, [&]() { aliveA = false; }, [&]() { doneA = true; });
        RefPtr<SimpleTask> taskB = new SimpleTask(
            [&]() { aliveB = true; },
            [&]() { aliveB = false; },
            [&]()
            {
                CHECK(doneA);
                doneB = true;
            }
        );
        TaskHandle taskHandleA = pool.submitTask(taskA);
        taskA.setNull();
        TaskHandle taskHandleB = pool.submitTask(taskB, &taskHandleA, 1);
        taskB.setNull();
        pool.releaseTask(taskHandleA);
        pool.waitForCompletion(taskHandleB);
        CHECK(doneB);
        pool.releaseTask(taskHandleB);
        CHECK(!aliveA);
        CHECK(!aliveB);
    }

    SUBCASE("complex dependency")
    {
        static constexpr int N = 100;
        static constexpr int M = 10;
        std::atomic<bool> aliveInner[N][M];
        std::atomic<bool> doneInner[N][M];
        std::atomic<bool> aliveOuter[N];
        std::atomic<bool> doneOuter[N];
        TaskHandle taskHandlesOuter[N];
        for (int i = 0; i < N; ++i)
        {
            TaskHandle taskHandlesInner[M];
            for (int j = 0; j < M; j++)
            {
                aliveInner[i][j] = false;
                doneInner[i][j] = false;
                RefPtr<SimpleTask> taskInner = new SimpleTask(
                    [&, i, j]() { aliveInner[i][j] = true; },
                    [&, i, j]() { aliveInner[i][j] = false; },
                    [&, i, j]() { doneInner[i][j] = true; }
                );
                taskHandlesInner[j] = pool.submitTask(taskInner);
            }
            aliveOuter[i] = false;
            doneOuter[i] = false;
            RefPtr<SimpleTask> taskOuter = new SimpleTask(
                [&, i]() { aliveOuter[i] = true; },
                [&, i]() { aliveOuter[i] = false; },
                [&, i]()
                {
                    for (int k = 0; k < M; ++k)
                        CHECK(doneInner[i][k]);
                    doneOuter[i] = true;
                }
            );
            taskHandlesOuter[i] = pool.submitTask(taskOuter, taskHandlesInner, M);
            for (int j = 0; j < M; j++)
            {
                pool.releaseTask(taskHandlesInner[j]);
            }
        }
        pool.waitForCompletion(taskHandlesOuter, N);
        for (int i = 0; i < N; ++i)
        {
            pool.releaseTask(taskHandlesOuter[i]);
            CHECK(doneOuter[i]);
            CHECK(!aliveOuter[i]);
        }
    }
}
