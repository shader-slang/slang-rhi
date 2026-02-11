#include "thread-pool.h"

#include <algorithm>
#include <cassert>

namespace rhi {

ITaskPool* ThreadPool::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == ITaskPool::getTypeGuid())
        return static_cast<ITaskPool*>(this);
    return nullptr;
}

ThreadPool::ThreadPool(uint32_t threadCount)
{
    if (threadCount == 0)
        threadCount = std::max(1u, std::thread::hardware_concurrency());

    m_workers.reserve(threadCount);
    for (uint32_t i = 0; i < threadCount; ++i)
        m_workers.emplace_back(&ThreadPool::workerLoop, this);
}

ThreadPool::~ThreadPool()
{
    // Signal all workers to shut down.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shutdown = true;
    }
    m_workerCV.notify_all();

    for (auto& worker : m_workers)
        worker.join();
}

void ThreadPool::workerLoop()
{
    for (;;)
    {
        Task* task = nullptr;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_workerCV.wait(lock, [this] { return !m_readyQueue.empty() || m_shutdown; });

            if (m_shutdown && m_readyQueue.empty())
                return;

            task = m_readyQueue.front();
            m_readyQueue.pop();
        }

        // Execute the task function outside the lock.
        assert(task && task->func);
        task->func(task->payload);

        // Mark done and process dependents.
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            task->done = true;

            for (Task* dependent : task->dependents)
            {
                dependent->remainingDeps--;
                if (dependent->remainingDeps == 0)
                    m_readyQueue.push(dependent);
            }

            m_pendingCount--;

            // Wake workers for newly ready dependents.
            if (!task->dependents.empty())
                m_workerCV.notify_all();
        }

        // Wake any threads in waitTask() / waitAll().
        m_completionCV.notify_all();
    }
}

ITaskPool::TaskHandle ThreadPool::submitTask(
    void (*func)(void*),
    void* payload,
    void (*payloadDeleter)(void*),
    TaskHandle* deps,
    size_t depsCount
)
{
    assert(func);
    assert(depsCount == 0 || deps);

    Task* task = new Task();
    task->func = func;
    task->payload = payload;
    task->payloadDeleter = payloadDeleter;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Register dependencies.
        for (size_t i = 0; i < depsCount; ++i)
        {
            Task* dep = static_cast<Task*>(deps[i]);
            if (dep->done)
                continue;
            dep->dependents.push_back(task);
            task->remainingDeps++;
        }

        m_pendingCount++;

        if (task->remainingDeps == 0)
        {
            m_readyQueue.push(task);
            m_workerCV.notify_one();
        }
    }

    return static_cast<TaskHandle>(task);
}

void* ThreadPool::getTaskPayload(TaskHandle handle)
{
    assert(handle);
    Task* task = static_cast<Task*>(handle);
    return task->payload;
}

void ThreadPool::releaseTask(TaskHandle handle)
{
    assert(handle);
    Task* task = static_cast<Task*>(handle);
    if (task->payloadDeleter)
        task->payloadDeleter(task->payload);
    delete task;
}

void ThreadPool::waitTask(TaskHandle handle)
{
    assert(handle);
    Task* task = static_cast<Task*>(handle);

    std::unique_lock<std::mutex> lock(m_mutex);
    m_completionCV.wait(lock, [task] { return task->done; });
}

bool ThreadPool::isTaskDone(TaskHandle handle)
{
    assert(handle);
    Task* task = static_cast<Task*>(handle);

    std::lock_guard<std::mutex> lock(m_mutex);
    return task->done;
}

void ThreadPool::waitAll()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_completionCV.wait(lock, [this] { return m_pendingCount == 0; });
}

} // namespace rhi
