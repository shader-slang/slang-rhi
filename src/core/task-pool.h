#pragma once

#include "common.h"

namespace rhi {

class BlockingTaskPool : public ITaskPool, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL

    ITaskPool* getInterface(const Guid& guid);

public:
    TaskHandle submitTask(
        void (*func)(void*),
        void* payload,
        void (*payloadDeleter)(void*),
        TaskHandle* deps,
        size_t depsCount
    ) override;

    void* getTaskPayload(TaskHandle task) override;

    void releaseTask(TaskHandle task) override;

    void waitTask(TaskHandle task) override;

    bool isTaskDone(TaskHandle task) override;

    void waitAll() override;

private:
    struct Task;
};

/// Set the global task scheduler.
/// Must be called before first accessing the global task pool.
Result setGlobalTaskPool(ITaskPool* taskPool);

/// Returns the global task pool.
ITaskPool* globalTaskPool();

} // namespace rhi
