#include "task-pool.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace rhi {

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
    size_t depsCount
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

    // Mutex and condition variable for waitTask().
    std::mutex waitMutex;
    std::condition_variable waitCV;

    // List of tasks that depend on this task.
    std::vector<Task*> children;
    std::mutex childrenMutex;
};

struct ThreadedTaskPool::Pool
{
    // Queue of tasks ready for execution.
    std::queue<ThreadedTaskPool::Task*> m_queue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCV;

    // Flag to signal worker threads to stop.
    std::atomic<bool> m_stop{false};

    // Worker threads.
    std::vector<std::thread> m_workerThreads;

    // Total number of tasks not yet completed.
    std::atomic<size_t> m_tasksRemaining{0};

    // Mutex and condition variable for waitAll().
    std::mutex m_waitMutex;
    std::condition_variable m_waitCV;

    void workerThread();

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
        }
    }

    Task* submitTask(void (*func)(void*), void* payload, void (*payloadDeleter)(void*), Task** deps, size_t depsCount)
    {
        SLANG_RHI_ASSERT(func);
        SLANG_RHI_ASSERT(depsCount == 0 || deps);

        Task* task = new Task();

        task->func = func;
        task->payload = payload;
        task->payloadDeleter = payloadDeleter;
        task->pool = this;
        task->depsRemaining = depsCount;

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

    void waitTask(Task* task)
    {
        SLANG_RHI_ASSERT(task);
        SLANG_RHI_ASSERT(task->pool == this);

        std::unique_lock<std::mutex> lock(task->waitMutex);
        task->waitCV.wait(
            lock,
            [task]
            {
                return task->done.load(std::memory_order_acquire);
            }
        );
    }

    void waitAll()
    {
        std::unique_lock<std::mutex> lock(m_waitMutex);
        m_waitCV.wait(
            lock,
            [this]
            {
                return m_tasksRemaining.load(std::memory_order_acquire) == 0;
            }
        );
    }
};

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
        // Execute the task function.
        task->func(task->payload);
        // Mark the task as done and notify child tasks.
        {
            std::lock_guard<std::mutex> lock(task->childrenMutex);
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
        // Notify waitTask() waiters.
        {
            std::lock_guard<std::mutex> lock(task->waitMutex);
            task->waitCV.notify_all();
        }
        // Release the pool's reference.
        // This must happen before decrementing m_tasksRemaining so that
        // waitAll() only returns after all payload deleters have been called.
        releaseTask(task);
        // Decrement the remaining task counter and notify waiters.
        if (m_tasksRemaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            std::lock_guard<std::mutex> lock(m_waitMutex);
            m_waitCV.notify_all();
        }
    }
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
    size_t depsCount
)
{
    return m_pool->submitTask(func, payload, payloadDeleter, (Task**)deps, depsCount);
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

// ----------------------------------------------------------------------------
// Global task pool
// ----------------------------------------------------------------------------

static std::mutex s_globalTaskPoolMutex;
static ComPtr<ITaskPool> s_globalTaskPool;
static std::atomic<ITaskPool*> s_cachedGlobalTaskPool{nullptr};

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
