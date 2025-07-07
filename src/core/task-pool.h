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
        size_t depsCount
    ) override;

    virtual SLANG_NO_THROW void* SLANG_MCALL getTaskPayload(TaskHandle task) override;

    virtual SLANG_NO_THROW void SLANG_MCALL releaseTask(TaskHandle task) override;

    virtual SLANG_NO_THROW void SLANG_MCALL waitTask(TaskHandle task) override;

    virtual SLANG_NO_THROW bool SLANG_MCALL isTaskDone(TaskHandle task) override;

    virtual SLANG_NO_THROW void SLANG_MCALL waitAll() override;

private:
    struct Task;
};

/// Set the global task scheduler.
/// Must be called before first accessing the global task pool.
Result setGlobalTaskPool(ITaskPool* taskPool);

/// Returns the global task pool.
ITaskPool* globalTaskPool();

} // namespace rhi
