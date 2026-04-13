#include "task-pool.h"

#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace rhi {

// Track work-stealing nesting depth per thread.
// Only the outermost wait call (depth 0) is allowed to steal and execute tasks.
// This prevents circular steal chains where a stolen task's callback waits on
// a task that is already in-flight higher up the same thread's call stack.
static thread_local int tls_stealDepth = 0;

// ----------------------------------------------------------------------------
// BlockingTaskPool
// ----------------------------------------------------------------------------

struct BlockingTaskPool::Task
{
    void* payload;
    void (*payloadDeleter)(void*);
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
    TaskHandle* deps,
    size_t depsCount,
    TaskGroupHandle group
)
{
    SLANG_RHI_ASSERT(func);
    SLANG_RHI_ASSERT(depsCount == 0 || deps);
    for (size_t i = 0; i < depsCount; i++)
    {
        SLANG_RHI_ASSERT(deps[i]);
    }

    // Create task just to defer the payload deletion.
    Task* task = new Task();
    task->payload = payload;
    task->payloadDeleter = payloadDeleter;

    // Execute the task function.
    func(payload);

    return task;
}

void* BlockingTaskPool::getTaskPayload(TaskHandle task)
{
    SLANG_RHI_ASSERT(task);

    Task* taskImpl = static_cast<Task*>(task);
    return taskImpl->payload;
}

void BlockingTaskPool::releaseTask(TaskHandle task)
{
    SLANG_RHI_ASSERT(task);

    Task* taskImpl = static_cast<Task*>(task);
    if (taskImpl->payloadDeleter)
    {
        taskImpl->payloadDeleter(taskImpl->payload);
    }
    delete taskImpl;
}

void BlockingTaskPool::waitTask(TaskHandle task)
{
    SLANG_UNUSED(task);
}

bool BlockingTaskPool::isTaskDone(TaskHandle task)
{
    return true;
}

void BlockingTaskPool::waitAll() {}

ITaskPool::TaskGroupHandle BlockingTaskPool::createTaskGroup()
{
    return new char;
}

void BlockingTaskPool::waitTaskGroup(TaskGroupHandle group)
{
    SLANG_RHI_ASSERT(group);
    SLANG_UNUSED(group);
}

void BlockingTaskPool::releaseTaskGroup(TaskGroupHandle group)
{
    SLANG_RHI_ASSERT(group);
    delete static_cast<char*>(group);
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

    // Number of dependencies that are not yet finished.
    std::atomic<size_t> depsRemaining{0};

    // Flag indicating the task has finished.
    std::atomic<bool> done{false};

    // List of tasks that depend on this task.
    std::vector<Task*> children;
    std::mutex childrenMutex;

    // Optional task group this task belongs to.
    struct TaskGroup* group = nullptr;
};

struct TaskGroup
{
    std::atomic<size_t> pending{0};
};

struct ThreadedTaskPool::Pool
{
    // Queue of tasks ready for execution.
    std::queue<ThreadedTaskPool::Task*> m_queue;
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

    // Try to dequeue a ready task from the queue. Returns nullptr if the queue is empty.
    Task* tryDequeue();

    // Execute a task and perform all completion bookkeeping (done flag, children,
    // group counter, tasksRemaining counter, reference release). Used by both
    // workerThread() and work-stealing wait loops.
    void executeTask(Task* task);

    void waitTaskGroup(TaskGroup* group);

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
            m_queue.pop();
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
        {
            if (task->payloadDeleter)
            {
                task->payloadDeleter(task->payload);
            }
            delete task;
        }
    }

    void enqueue(Task* task)
    {
        SLANG_RHI_ASSERT(task);
        SLANG_RHI_ASSERT(task->pool == this);

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_queue.push(task);
            m_queueCV.notify_one();
            // Only wake one work-stealing waiter per enqueue to avoid thundering herd.
            // The completion path in executeTask() uses notify_all() to wake all waiters
            // so they can recheck their specific conditions (done, pending==0, etc.).
            m_stealCV.notify_one();
        }
    }

    Task* submitTask(
        void (*func)(void*),
        void* payload,
        void (*payloadDeleter)(void*),
        Task** deps,
        size_t depsCount,
        TaskGroup* group
    )
    {
        SLANG_RHI_ASSERT(func);
        SLANG_RHI_ASSERT(depsCount == 0 || deps);
        SLANG_RHI_ASSERT(!m_stop.load(std::memory_order_relaxed));

        Task* task = new Task();

        task->func = func;
        task->payload = payload;
        task->payloadDeleter = payloadDeleter;
        task->pool = this;
        task->depsRemaining = depsCount;
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

        m_tasksRemaining.fetch_add(1, std::memory_order_relaxed);

        if (depsCount == 0)
        {
            // If there are no dependencies, enqueue the task immediately.
            enqueue(task);
        }
        else
        {
            // Process dependencies.
            bool readyToEnqueue = false;
            for (size_t i = 0; i < depsCount; i++)
            {
                Task* dep = deps[i];
                SLANG_RHI_ASSERT(dep);
                SLANG_RHI_ASSERT(dep->refCount.load(std::memory_order_acquire) > 0);
                SLANG_RHI_ASSERT(dep->pool == this);
                {
                    std::lock_guard<std::mutex> lock(dep->childrenMutex);
                    if (!dep->done.load(std::memory_order_acquire))
                    {
                        // Add an extra reference that will be released when the dependency finishes.
                        retainTask(task);
                        dep->children.push_back(task);
                    }
                    else
                    {
                        // Dependency is already done, decrement the counter.
                        // Relaxed ordering is safe here because dep->childrenMutex provides
                        // the necessary acquire/release synchronization.
                        if (task->depsRemaining.fetch_sub(1, std::memory_order_relaxed) == 1)
                        {
                            readyToEnqueue = true;
                        }
                    }
                }
            }
            // Enqueue outside the dep lock scope to avoid use-after-free.
            // Enqueueing while holding dep->childrenMutex could allow a worker to
            // execute the task and release the dep before we unlock.
            if (readyToEnqueue)
            {
                enqueue(task);
            }
        }
        return task;
    }

    bool isTaskDone(Task* task)
    {
        SLANG_RHI_ASSERT(task);
        SLANG_RHI_ASSERT(task->pool == this);

        return task->done.load(std::memory_order_acquire);
    }

    // Work-stealing wait loop. Spins until `isDone` returns true, stealing and
    // executing queued tasks when possible (only at steal-depth 0 to prevent
    // circular chains). Falls back to blocking on m_stealCV when no work is
    // available. At depth > 0 the predicate excludes !m_queue.empty() to avoid
    // a spin-loop that would starve worker threads.
    template<typename Pred>
    void waitWithStealing(Pred isDone)
    {
        while (!isDone())
        {
            if (tls_stealDepth == 0)
            {
                if (Task* stolen = tryDequeue())
                {
                    executeTask(stolen);
                    continue;
                }
            }
            std::unique_lock<std::mutex> lock(m_queueMutex);
            if (tls_stealDepth == 0)
            {
                m_stealCV.wait(
                    lock,
                    [&]
                    {
                        return isDone() || !m_queue.empty();
                    }
                );
            }
            else
            {
                m_stealCV.wait(lock, isDone);
            }
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

ThreadedTaskPool::Task* ThreadedTaskPool::Pool::tryDequeue()
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_queue.empty())
        return nullptr;
    Task* task = m_queue.front();
    m_queue.pop();
    return task;
}

void ThreadedTaskPool::Pool::executeTask(Task* task)
{
    // Execute the task function.
    // Wrap in try/catch to ensure the worker thread survives and the
    // task-completion bookkeeping (done flag, children, group, counters)
    // always runs. Without this, an exception would deadlock waiters.
    // NOTE: If a task throws, it is still marked as done and its children
    // will execute. There is currently no failure propagation mechanism.
    // Increment steal depth so that any waitTask/waitAll/waitTaskGroup called
    // from the task callback cannot steal tasks. This prevents circular steal
    // chains where a stolen task's callback waits on a task already in-flight
    // on the same thread's call stack.
    tls_stealDepth++;
    try
    {
        task->func(task->payload);
    } catch (...)
    {
        SLANG_RHI_ASSERT_FAILURE("Task threw an exception");
    }
    tls_stealDepth--;
    // Capture the group pointer before we potentially release the task.
    TaskGroup* group = task->group;
    // Mark the task as done and notify child tasks.
    // We hold childrenMutex to safely process the children list and to
    // synchronize with submitTask() which checks done under the same lock.
    {
        std::lock_guard<std::mutex> childLock(task->childrenMutex);
        task->done.store(true, std::memory_order_release);
        for (Task* child : task->children)
        {
            // Decrement the child's dependency counter.
            if (child->depsRemaining.fetch_sub(1, std::memory_order_relaxed) == 1)
            {
                // All dependencies satisfied; enqueue the child.
                enqueue(child);
            }
            // Release the extra reference taken when adding as a dependency.
            releaseTask(child);
        }
        task->children.clear();
    }
    // Release the pool's reference.
    // This must happen before decrementing m_tasksRemaining so that
    // waitAll() only returns after all payload deleters have been called.
    releaseTask(task);
    // Decrement the group pending counter.
    // Safety: group is user-managed, but this is safe because waitTaskGroup()
    // only returns when pending==0, which requires ALL tasks in the group to
    // have completed this fetch_sub. Therefore no task can still be accessing
    // the group when the user calls releaseTaskGroup() after waitTaskGroup().
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
            m_queue.pop();
        }
        executeTask(task);
    }
}

void ThreadedTaskPool::Pool::waitTaskGroup(TaskGroup* group)
{
    SLANG_RHI_ASSERT(group);

    waitWithStealing(
        [group]
        {
            return group->pending.load(std::memory_order_acquire) == 0;
        }
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
    TaskHandle* deps,
    size_t depsCount,
    TaskGroupHandle group
)
{
    return m_pool->submitTask(func, payload, payloadDeleter, (Task**)deps, depsCount, static_cast<TaskGroup*>(group));
}

void* ThreadedTaskPool::getTaskPayload(TaskHandle task)
{
    SLANG_RHI_ASSERT(task);

    return static_cast<Task*>(task)->payload;
}

void ThreadedTaskPool::releaseTask(TaskHandle task)
{
    m_pool->releaseTask(static_cast<Task*>(task));
}

void ThreadedTaskPool::waitTask(TaskHandle task)
{
    m_pool->waitTask(static_cast<Task*>(task));
}

bool ThreadedTaskPool::isTaskDone(TaskHandle task)
{
    return m_pool->isTaskDone(static_cast<Task*>(task));
}

void ThreadedTaskPool::waitAll()
{
    m_pool->waitAll();
}

ITaskPool::TaskGroupHandle ThreadedTaskPool::createTaskGroup()
{
    return new TaskGroup();
}

void ThreadedTaskPool::waitTaskGroup(TaskGroupHandle group)
{
    SLANG_RHI_ASSERT(group);

    m_pool->waitTaskGroup(static_cast<TaskGroup*>(group));
}

void ThreadedTaskPool::releaseTaskGroup(TaskGroupHandle group)
{
    SLANG_RHI_ASSERT(group);

    TaskGroup* g = static_cast<TaskGroup*>(group);
    SLANG_RHI_ASSERT(g->pending.load(std::memory_order_acquire) == 0);
    delete g;
}

// ----------------------------------------------------------------------------
// Global task pool
// ----------------------------------------------------------------------------

static std::mutex s_globalTaskPoolMutex;
static ComPtr<ITaskPool> s_globalTaskPool;
static std::atomic<ITaskPool*> s_cachedGlobalTaskPool{nullptr};

// WARNING: setGlobalTaskPool must only be called when no devices are alive
// and no other threads are using the global task pool. Calling it concurrently
// with globalTaskPool() may result in use-after-free.
Result setGlobalTaskPool(ITaskPool* taskPool)
{
    std::lock_guard<std::mutex> lock(s_globalTaskPoolMutex);
    s_globalTaskPool = taskPool;
    s_cachedGlobalTaskPool.store(taskPool, std::memory_order_release);
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
    ITaskPool* cached = s_cachedGlobalTaskPool.load(std::memory_order_acquire);
    if (cached)
    {
        return cached;
    }
    std::lock_guard<std::mutex> lock(s_globalTaskPoolMutex);
    if (!s_globalTaskPool)
    {
        s_globalTaskPool = new ThreadedTaskPool(-1);
    }
    s_cachedGlobalTaskPool.store(s_globalTaskPool.get(), std::memory_order_release);
    return s_globalTaskPool.get();
}

} // namespace rhi
