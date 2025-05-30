#include "task-pool.h"

#include <mutex>

namespace rhi {

// BlockingTaskPool

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

    // Dependent tasks are guaranteed to be done.
    SLANG_UNUSED(deps);
    SLANG_UNUSED(depsCount);

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


static std::mutex s_globalTaskPoolMutex;
static ComPtr<ITaskPool> s_globalTaskPool;

Result setGlobalTaskPool(ITaskPool* taskPool)
{
    std::lock_guard<std::mutex> lock(s_globalTaskPoolMutex);
    if (s_globalTaskPool)
    {
        return SLANG_FAIL;
    }
    s_globalTaskPool = taskPool;
    return SLANG_OK;
}

ITaskPool* globalTaskPool()
{
    static std::atomic<ITaskPool*> taskPool;
    if (taskPool)
    {
        return taskPool;
    }
    std::lock_guard<std::mutex> lock(s_globalTaskPoolMutex);
    if (!s_globalTaskPool)
    {
        s_globalTaskPool = new BlockingTaskPool();
    }
    taskPool = s_globalTaskPool.get();
    return taskPool;
}

} // namespace rhi
