#pragma once

#include "core/com-object.h"

#include <slang-rhi.h>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace rhi {

/// A parallel ITaskPool implementation backed by a real thread pool.
/// Workers pull tasks from a shared queue. Task dependencies are honored:
/// a task is not scheduled until all its dependency tasks are complete.
///
/// The thread count can be changed dynamically via setThreadCount().
/// When the thread count is 0, the pool operates in serial mode:
/// tasks are executed immediately on the calling thread (like BlockingTaskPool).
class ThreadPool : public ITaskPool, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL

    ITaskPool* getInterface(const Guid& guid);

    /// Create a thread pool.
    /// \param threadCount Number of worker threads. 0 means serial (execute inline).
    explicit ThreadPool(uint32_t threadCount = 0);
    ~ThreadPool();

    /// Dynamically change the number of worker threads.
    /// Waits for all pending tasks to complete, shuts down existing workers,
    /// then starts the new number of workers. 0 means serial mode.
    void setThreadCount(uint32_t count);

    /// Returns the current number of worker threads (0 = serial).
    uint32_t getThreadCount() const;

    // ITaskPool interface
    virtual SLANG_NO_THROW TaskHandle SLANG_MCALL submitTask(
        void (*func)(void*),
        void* payload,
        void (*payloadDeleter)(void*),
        TaskHandle* deps,
        size_t depsCount
    ) override;

    virtual SLANG_NO_THROW void* SLANG_MCALL getTaskPayload(TaskHandle task) override;

    virtual SLANG_NO_THROW void SLANG_MCALL releaseTask(TaskHandle task) override;

    virtual SLANG_NO_THROW void SLANG_MCALL waitTask(TaskHandle task) override;

    virtual SLANG_NO_THROW bool SLANG_MCALL isTaskDone(TaskHandle task) override;

    virtual SLANG_NO_THROW void SLANG_MCALL waitAll() override;

private:
    struct Task
    {
        void (*func)(void*) = nullptr;
        void* payload = nullptr;
        void (*payloadDeleter)(void*) = nullptr;

        // Dependency tracking — protected by m_mutex.
        std::vector<Task*> dependents;
        int remainingDeps = 0;
        bool done = false;
    };

    void workerLoop();
    void shutdownWorkers();  // Signal workers to stop and join all threads.
    void startWorkers(uint32_t count); // Spawn count worker threads.

    std::vector<std::thread> m_workers;
    std::queue<Task*> m_readyQueue;
    mutable std::mutex m_mutex;
    std::condition_variable m_workerCV;     // Workers wait for ready tasks.
    std::condition_variable m_completionCV; // Waiters wait for task completion.
    bool m_shutdown = false;
    int m_pendingCount = 0;
};

} // namespace rhi
