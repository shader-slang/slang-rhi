#pragma once

#include "common.h"

namespace rhi {

class BlockingTaskPool : public ITaskPool, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL

    ITaskPool* getInterface(const Guid& guid);

public:
    virtual SLANG_NO_THROW TaskHandle SLANG_MCALL submitTask(
        void (*func)(void*),
        void* payload,
        void (*payloadDeleter)(void*),
        TaskHandle* deps,
        size_t depsCount,
        TaskGroupHandle group = nullptr
    ) override;

    virtual SLANG_NO_THROW void* SLANG_MCALL getTaskPayload(TaskHandle task) override;

    virtual SLANG_NO_THROW void SLANG_MCALL releaseTask(TaskHandle task) override;

    virtual SLANG_NO_THROW void SLANG_MCALL waitTask(TaskHandle task) override;

    virtual SLANG_NO_THROW bool SLANG_MCALL isTaskDone(TaskHandle task) override;

    virtual SLANG_NO_THROW void SLANG_MCALL waitAll() override;

    virtual SLANG_NO_THROW TaskGroupHandle SLANG_MCALL createTaskGroup() override;

    virtual SLANG_NO_THROW void SLANG_MCALL waitTaskGroup(TaskGroupHandle group) override;

    virtual SLANG_NO_THROW void SLANG_MCALL releaseTaskGroup(TaskGroupHandle group) override;

private:
    struct Task;
};

class ThreadedTaskPool : public ITaskPool, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL

    ITaskPool* getInterface(const Guid& guid);

public:
    ThreadedTaskPool(int workerCount = -1);
    ~ThreadedTaskPool() override;

    virtual SLANG_NO_THROW TaskHandle SLANG_MCALL submitTask(
        void (*func)(void*),
        void* payload,
        void (*payloadDeleter)(void*),
        TaskHandle* deps,
        size_t depsCount,
        TaskGroupHandle group = nullptr
    ) override;

    virtual SLANG_NO_THROW void* SLANG_MCALL getTaskPayload(TaskHandle task) override;

    virtual SLANG_NO_THROW void SLANG_MCALL releaseTask(TaskHandle task) override;

    virtual SLANG_NO_THROW void SLANG_MCALL waitTask(TaskHandle task) override;

    virtual SLANG_NO_THROW bool SLANG_MCALL isTaskDone(TaskHandle task) override;

    virtual SLANG_NO_THROW void SLANG_MCALL waitAll() override;

    virtual SLANG_NO_THROW TaskGroupHandle SLANG_MCALL createTaskGroup() override;

    virtual SLANG_NO_THROW void SLANG_MCALL waitTaskGroup(TaskGroupHandle group) override;

    virtual SLANG_NO_THROW void SLANG_MCALL releaseTaskGroup(TaskGroupHandle group) override;

private:
    struct Task;
    struct Pool;

    Pool* m_pool;
};

/// Set the global task scheduler.
/// Can be called to replace the current task pool when no devices are alive.
Result setGlobalTaskPool(ITaskPool* taskPool);

/// Initialize the global task pool with the given worker count.
/// A value of 0 creates a BlockingTaskPool.
/// A value of -1 creates a ThreadedTaskPool with std::thread::hardware_concurrency() worker threads.
/// Any positive value creates a ThreadedTaskPool with that many worker threads.
/// Can be called to replace the current task pool when no devices are alive.
Result initGlobalTaskPool(int workerCount);

/// Returns the global task pool.
ITaskPool* globalTaskPool();

} // namespace rhi
