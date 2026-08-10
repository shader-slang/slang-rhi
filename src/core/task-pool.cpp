#include "task-pool.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>

namespace rhi {

// Track work-stealing nesting depth per thread. Outermost waits may steal any
// ready task. A nested task-group wait may steal only ready tasks from that
// group, which lets dynamically spawned work make progress without executing
// an unrelated task that could wait on the current callback.
static thread_local int tls_stealDepth = 0;

struct TaskGroup
{
    explicit TaskGroup(const void* owner_)
        : owner(owner_)
    {
    }

    const void* owner;
    std::atomic<size_t> pending{0};
};

// ----------------------------------------------------------------------------
// BlockingTaskPool
// ----------------------------------------------------------------------------

struct BlockingTaskPool::Task
{
    const BlockingTaskPool* owner;
};

ITaskPool* BlockingTaskPool::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == ITaskPool::getTypeGuid())
        return static_cast<ITaskPool*>(this);
    return nullptr;
}

ITaskPool::TaskHandle BlockingTaskPool::submitTask(
    void (*func)(void*),
    void* payload,
    void (*payloadDeleter)(void*),
    TaskGroupHandle group
)
{
    SLANG_RHI_ASSERT(func);
    SLANG_RHI_ASSERT(!group || static_cast<TaskGroup*>(group)->owner == this);

    // Create a completion token for the caller.
    Task* task = new Task();
    task->owner = this;

    func(payload);
    if (payloadDeleter)
        payloadDeleter(payload);

    return task;
}

void BlockingTaskPool::releaseTask(TaskHandle task)
{
    SLANG_RHI_ASSERT(task);

    Task* taskImpl = static_cast<Task*>(task);
    SLANG_RHI_ASSERT(taskImpl->owner == this);
    delete taskImpl;
}

void BlockingTaskPool::waitAndReleaseTask(TaskHandle task)
{
    releaseTask(task);
}

ITaskPool::TaskGroupHandle BlockingTaskPool::createTaskGroup()
{
    return new TaskGroup(this);
}

void BlockingTaskPool::waitAndReleaseTaskGroup(TaskGroupHandle group)
{
    SLANG_RHI_ASSERT(group);
    TaskGroup* g = static_cast<TaskGroup*>(group);
    SLANG_RHI_ASSERT(g->owner == this);
    delete g;
}

// ----------------------------------------------------------------------------
// ThreadedTaskPool
// ----------------------------------------------------------------------------

struct ThreadedTaskPool::Task
{
    // Function to execute.
    void (*func)(void*) = nullptr;
    // Pointer to payload data.
    void* payload = nullptr;
    // Optional deleter for the payload.
    void (*payloadDeleter)(void*) = nullptr;

    // Pool that owns the task.
    Pool* pool = nullptr;

    // Reference counter.
    std::atomic<size_t> refCount{0};

    // Flag indicating the task has finished.
    std::atomic<bool> done{false};

    // Optional task group this task belongs to.
    struct TaskGroup* group = nullptr;
};

struct ThreadedTaskPool::Pool
{
    // Queue of tasks ready for execution.
    std::deque<ThreadedTaskPool::Task*> m_queue;
    std::mutex m_queueMutex;
    // Condition variable for worker threads (notified when queue gets items or stop).
    std::condition_variable m_queueCV;
    // Condition variable for work-stealing waiters (notified on enqueue and task completion).
    // Shares m_queueMutex with m_queueCV but only wakes work-stealing threads, avoiding
    // thundering herd on worker threads.
    std::condition_variable m_stealCV;

    // Flag to signal worker threads to stop.
    std::atomic<bool> m_stop{false};

    // Worker threads.
    std::vector<std::thread> m_workerThreads;

    // Total number of tasks not yet completed.
    std::atomic<size_t> m_tasksRemaining{0};

    void workerThread();

    // Try to dequeue a ready task. When group is non-null, only tasks from that
    // group are eligible. Returns nullptr if no matching task is ready.
    Task* tryDequeue(TaskGroup* group = nullptr);

    // Must be called with m_queueMutex held.
    bool hasQueuedTask(TaskGroup* group) const
    {
        if (!group)
            return !m_queue.empty();
        return std::any_of(
            m_queue.begin(),
            m_queue.end(),
            [group](const Task* task)
            {
                return task->group == group;
            }
        );
    }

    // Execute a task and perform all completion bookkeeping (payload cleanup,
    // done flag, group counter, tasksRemaining counter, reference release). Used by both
    // workerThread() and work-stealing wait loops.
    void executeTask(Task* task);

    void waitGroup(TaskGroup* group);

    Pool(int workerCount)
    {
        if (workerCount <= 0)
        {
            workerCount = static_cast<int>(std::thread::hardware_concurrency());
            if (workerCount <= 0)
                workerCount = 1;
        }
        for (int i = 0; i < workerCount; i++)
        {
            m_workerThreads.emplace_back(
                [this]()
                {
                    workerThread();
                }
            );
        }
    }

    ~Pool()
    {
        // Drain all pending tasks before shutting down.
        waitAll();
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_stop.store(true);
            m_queueCV.notify_all();
        }
        for (std::thread& worker : m_workerThreads)
        {
            if (worker.joinable())
                worker.join();
        }
        while (!m_queue.empty())
        {
            Task* task = m_queue.front();
            m_queue.pop_front();
            // Null check to silence GCC -Wstringop-overflow (it inlines releaseTask
            // and cannot prove queue elements are non-null).
            if (task)
                releaseTask(task);
        }
    }

    void retainTask(Task* task, size_t count = 1)
    {
        SLANG_RHI_ASSERT(task);
        SLANG_RHI_ASSERT(task->pool == this);

        task->refCount.fetch_add(count, std::memory_order_relaxed);
    }

    void releaseTask(Task* task)
    {
        SLANG_RHI_ASSERT(task);
        SLANG_RHI_ASSERT(task->pool == this);

        if (task->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete task;
    }

    void enqueue(Task* task)
    {
        SLANG_RHI_ASSERT(task);
        SLANG_RHI_ASSERT(task->pool == this);

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_queue.push_back(task);
            m_queueCV.notify_one();
            // Nested work-stealing waiters filter by task group. Wake all waiters
            // because notify_one() could select a waiter for a different group.
            m_stealCV.notify_all();
        }
    }

    Task* submitTask(void (*func)(void*), void* payload, void (*payloadDeleter)(void*), TaskGroup* group)
    {
        SLANG_RHI_ASSERT(func);
        SLANG_RHI_ASSERT(!m_stop.load(std::memory_order_relaxed));
        SLANG_RHI_ASSERT(!group || group->owner == this);

        Task* task = new Task();

        task->func = func;
        task->payload = payload;
        task->payloadDeleter = payloadDeleter;
        task->pool = this;
        task->group = group;

        // Increment the group counter before enqueuing (critical for correctness).
        // Relaxed ordering is sufficient: the submitting thread has sequenced-before
        // visibility, and cross-thread synchronization is provided by m_queueMutex
        // in enqueue()/tryDequeue().
        if (group)
        {
            group->pending.fetch_add(1, std::memory_order_relaxed);
        }

        // Increment the reference count by 2.
        // One reference is for the pool, the other is for the caller.
        retainTask(task, 2);

        m_tasksRemaining.fetch_add(1, std::memory_order_release);

        enqueue(task);
        return task;
    }

    // Work-stealing wait loop. Outermost waits may steal any task. Nested waits
    // normally block to avoid circular execution chains, but a nested group
    // wait may steal tasks from that group only.
    template<typename Pred>
    void waitWithStealing(Pred isDone, TaskGroup* nestedGroup = nullptr)
    {
        while (!isDone())
        {
            TaskGroup* groupFilter = tls_stealDepth == 0 ? nullptr : nestedGroup;
            if (tls_stealDepth == 0 || groupFilter)
            {
                if (Task* stolen = tryDequeue(groupFilter))
                {
                    executeTask(stolen);
                    continue;
                }
            }
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_stealCV.wait(
                lock,
                [&]
                {
                    return isDone() || ((tls_stealDepth == 0 || groupFilter) && hasQueuedTask(groupFilter));
                }
            );
        }
    }

    void waitTask(Task* task)
    {
        SLANG_RHI_ASSERT(task);
        SLANG_RHI_ASSERT(task->pool == this);

        waitWithStealing(
            [task]
            {
                return task->done.load(std::memory_order_acquire);
            }
        );
    }

    void waitAll()
    {
        waitWithStealing(
            [this]
            {
                return m_tasksRemaining.load(std::memory_order_acquire) == 0;
            }
        );
    }
};

ThreadedTaskPool::Task* ThreadedTaskPool::Pool::tryDequeue(TaskGroup* group)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    auto it = group ? std::find_if(
                          m_queue.begin(),
                          m_queue.end(),
                          [group](const Task* task)
                          {
                              return task->group == group;
                          }
                      )
                    : m_queue.begin();
    if (it == m_queue.end())
        return nullptr;
    Task* task = *it;
    m_queue.erase(it);
    return task;
}

void ThreadedTaskPool::Pool::executeTask(Task* task)
{
    // Wrap callbacks in try/catch to ensure the worker thread survives and the
    // task-completion bookkeeping always runs. Without this, an exception would
    // deadlock waiters. There is currently no failure propagation mechanism.
    // Increment steal depth so nested waits do not steal unrelated tasks. A
    // nested task-group wait may still execute work from its own group.
    tls_stealDepth++;
    try
    {
        task->func(task->payload);
    } catch (const std::exception& e)
    {
        SLANG_RHI_ASSERT_FAILURE(e.what());
    } catch (...)
    {
        SLANG_RHI_ASSERT_FAILURE("Task threw an unknown exception");
    }
    try
    {
        if (task->payloadDeleter)
            task->payloadDeleter(task->payload);
    } catch (const std::exception& e)
    {
        SLANG_RHI_ASSERT_FAILURE(e.what());
    } catch (...)
    {
        SLANG_RHI_ASSERT_FAILURE("Task payload deleter threw an unknown exception");
    }
    tls_stealDepth--;

    // Capture the group pointer before we potentially release the task.
    TaskGroup* group = task->group;

    // Payload cleanup is part of task completion, so publish done only after the
    // task function and payload deleter have both returned.
    task->done.store(true, std::memory_order_release);

    // Release the pool's reference before decrementing the completion counters.
    releaseTask(task);
    // Decrement the group pending counter.
    // Safety: group is user-managed, but this is safe because waitGroup()
    // only returns when pending==0, which requires ALL tasks in the group to
    // have completed this fetch_sub. Therefore no task can still be accessing
    // the group when waitAndReleaseTaskGroup() deletes it.
    if (group)
    {
        group->pending.fetch_sub(1, std::memory_order_acq_rel);
    }
    // Decrement the remaining task counter.
    m_tasksRemaining.fetch_sub(1, std::memory_order_acq_rel);
    // Notify stealCV so that work-stealing wait loops wake up
    // and recheck their conditions. Must hold m_queueMutex to prevent
    // lost wakeups (notification arriving between predicate check and wait).
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_stealCV.notify_all();
    }
}

void ThreadedTaskPool::Pool::workerThread()
{
    while (true)
    {
        Task* task = nullptr;
        // Fetch next task from queue.
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCV.wait(
                lock,
                [this]
                {
                    return m_stop.load() || !m_queue.empty();
                }
            );
            if (m_stop.load() && m_queue.empty())
                return;
            task = m_queue.front();
            m_queue.pop_front();
        }
        executeTask(task);
    }
}

void ThreadedTaskPool::Pool::waitGroup(TaskGroup* group)
{
    SLANG_RHI_ASSERT(group);
    SLANG_RHI_ASSERT(group->owner == this);

    waitWithStealing(
        [group]
        {
            return group->pending.load(std::memory_order_acquire) == 0;
        },
        group
    );
}

ITaskPool* ThreadedTaskPool::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == ITaskPool::getTypeGuid())
        return static_cast<ITaskPool*>(this);
    return nullptr;
}

ThreadedTaskPool::ThreadedTaskPool(int workerCount)
{
    m_pool = new Pool(workerCount);
}

ThreadedTaskPool::~ThreadedTaskPool()
{
    delete m_pool;
}

ITaskPool::TaskHandle ThreadedTaskPool::submitTask(
    void (*func)(void*),
    void* payload,
    void (*payloadDeleter)(void*),
    TaskGroupHandle group
)
{
    return m_pool->submitTask(func, payload, payloadDeleter, static_cast<TaskGroup*>(group));
}

void ThreadedTaskPool::releaseTask(TaskHandle task)
{
    m_pool->releaseTask(static_cast<Task*>(task));
}

void ThreadedTaskPool::waitAndReleaseTask(TaskHandle task)
{
    Task* taskImpl = static_cast<Task*>(task);
    m_pool->waitTask(taskImpl);
    m_pool->releaseTask(taskImpl);
}

ITaskPool::TaskGroupHandle ThreadedTaskPool::createTaskGroup()
{
    return new TaskGroup(m_pool);
}

void ThreadedTaskPool::waitAndReleaseTaskGroup(TaskGroupHandle group)
{
    SLANG_RHI_ASSERT(group);
    SLANG_RHI_ASSERT(static_cast<TaskGroup*>(group)->owner == m_pool);

    TaskGroup* g = static_cast<TaskGroup*>(group);
    m_pool->waitGroup(g);
    SLANG_RHI_ASSERT(g->pending.load(std::memory_order_acquire) == 0);
    delete g;
}

// ----------------------------------------------------------------------------
// Global task pool
// ----------------------------------------------------------------------------

SLANG_RHI_STATIC_MUTEX_BEGIN
static std::mutex s_globalTaskPoolMutex;
SLANG_RHI_STATIC_MUTEX_END
static ITaskPool* s_globalTaskPool;

// WARNING: setGlobalTaskPool must only be called when no devices are alive
// and no other threads are using the global task pool. Calling it concurrently
// with globalTaskPool() may result in use-after-free.
Result setGlobalTaskPool(ITaskPool* taskPool)
{
    std::lock_guard<std::mutex> lock(s_globalTaskPoolMutex);
    if (s_globalTaskPool)
        s_globalTaskPool->release();
    s_globalTaskPool = taskPool;
    if (s_globalTaskPool)
        s_globalTaskPool->addRef();
    return SLANG_OK;
}

Result initGlobalTaskPool(int workerCount)
{
    ComPtr<ITaskPool> pool;
    if (workerCount == 0)
    {
        pool = new BlockingTaskPool();
    }
    else
    {
        pool = new ThreadedTaskPool(workerCount < 0 ? -1 : workerCount);
    }
    return setGlobalTaskPool(pool);
}

ITaskPool* globalTaskPool()
{
    std::lock_guard<std::mutex> lock(s_globalTaskPoolMutex);
    if (!s_globalTaskPool)
    {
        s_globalTaskPool = new ThreadedTaskPool(-1);
        s_globalTaskPool->addRef();
    }
    return s_globalTaskPool;
}

} // namespace rhi
