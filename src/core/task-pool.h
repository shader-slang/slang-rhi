#pragma once

#include "common.h"

namespace rhi {

class Task : public RefObject
{
public:
    virtual ~Task() {}

    virtual void run() = 0;

private:
    std::string m_name;
};

using TaskHandle = ITaskScheduler::TaskHandle;

class TaskPool
{
public:
    static constexpr uint32_t kAutoWorkerCount = uint32_t(-1);

    TaskPool(uint32_t workerCount = kAutoWorkerCount);
    TaskPool(ITaskScheduler* scheduler);
    ~TaskPool();

    TaskHandle submitTask(Task* task, TaskHandle* parentTaskHandles = nullptr, uint32_t parentTaskHandleCount = 0);
    void releaseTask(TaskHandle taskHandle);
    void waitForCompletion(TaskHandle taskHandle);
    void waitForCompletion(TaskHandle* taskHandles, uint32_t taskHandleCount);

private:
    ComPtr<ITaskScheduler> m_scheduler;
};

/// Set the global task pool worker count.
/// Must be called before first accessing the global task pool.
/// This is ignored if the task scheduler is set.
Result setGlobalTaskPoolWorkerCount(uint32_t count);

/// Set the global task scheduler.
/// Must be called before first accessing the global task pool.
Result setGlobalTaskScheduler(ITaskScheduler* scheduler);

/// Returns the global task pool.
TaskPool& globalTaskPool();

} // namespace rhi
