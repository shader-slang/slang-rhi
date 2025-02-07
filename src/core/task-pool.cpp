#include "task-pool.h"

#if 0
#include <nanothread/nanothread.h>
#endif

#include <memory>

namespace rhi {

class BlockingTaskScheduler : public ITaskScheduler, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL

    ITaskScheduler* getInterface(const Guid& guid)
    {
        if (guid == ISlangUnknown::getTypeGuid() || guid == ITaskScheduler::getTypeGuid())
            return static_cast<ITaskScheduler*>(this);
        return nullptr;
    }

    virtual SLANG_NO_THROW TaskHandle SLANG_MCALL
    submitTask(TaskHandle* parentTasks, uint32_t parentTaskCount, void (*run)(void*), void* payload) override
    {
        SLANG_UNUSED(parentTasks);
        SLANG_UNUSED(parentTaskCount);
        run(payload);
        return payload;
    }

    virtual SLANG_NO_THROW void SLANG_MCALL releaseTask(TaskHandle task) override { SLANG_UNUSED(task); }

    virtual SLANG_NO_THROW void SLANG_MCALL waitForCompletion(TaskHandle task) override { SLANG_UNUSED(task); }
};

#if 0
class NanoThreadTaskScheduler : public ITaskScheduler, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL

    ITaskScheduler* getInterface(const Guid& guid)
    {
        if (guid == ISlangUnknown::getTypeGuid() || guid == ITaskScheduler::getTypeGuid())
            return static_cast<ITaskScheduler*>(this);
        return nullptr;
    }

    NanoThreadTaskScheduler(uint32_t size) { m_pool = ::pool_create(size); }
    ~NanoThreadTaskScheduler() { ::pool_destroy(m_pool); }

    virtual SLANG_NO_THROW TaskHandle SLANG_MCALL
    submitTask(TaskHandle* parentTasks, uint32_t parentTaskCount, void (*run)(void*), void* payload) override
    {
        TaskInfo taskInfo{run, payload};
        if (parentTasks && parentTaskCount > 0)
        {
            return ::task_submit_dep(
                m_pool,
                (::Task**)parentTasks,
                parentTaskCount,
                1,
                runTask,
                &taskInfo,
                sizeof(TaskInfo),
                nullptr,
                1
            );
        }
        else
        {
            return ::task_submit(m_pool, 1, runTask, &taskInfo, sizeof(TaskInfo), nullptr, 1);
        }
    }

    virtual SLANG_NO_THROW void SLANG_MCALL releaseTask(TaskHandle task) override { ::task_release((::Task*)task); }

    virtual SLANG_NO_THROW void SLANG_MCALL waitForCompletion(TaskHandle task) override { ::task_wait((::Task*)task); }

private:
    struct TaskInfo
    {
        void (*run)(void*);
        void* payload;
    };

    static void runTask(uint32_t index, void* payload)
    {
        TaskInfo* taskInfo = (TaskInfo*)payload;
        taskInfo->run(taskInfo->payload);
    }

    ::Pool* m_pool;
};
#endif

class WaitTask : public Task
{
public:
    virtual void run() override {}
};

static void runTask(void* task)
{
    ((Task*)task)->run();
    ((Task*)task)->releaseReference();
}

TaskPool::TaskPool(uint32_t workerCount)
{
    m_scheduler = new BlockingTaskScheduler();
#if 0
    if (workerCount == 0)
    {
        m_scheduler = new BlockingTaskScheduler();
    }
    else
    {
        m_scheduler = new NanoThreadTaskScheduler(workerCount == kAutoWorkerCount ? NANOTHREAD_AUTO : workerCount);
    }
#endif
    SLANG_RHI_ASSERT(m_scheduler);
}

TaskPool::TaskPool(ITaskScheduler* scheduler)
    : m_scheduler(scheduler)
{
    SLANG_RHI_ASSERT(m_scheduler);
}

TaskPool::~TaskPool() {}

TaskHandle TaskPool::submitTask(Task* task, TaskHandle* parentTaskHandles, uint32_t parentTaskHandleCount)
{
    task->addReference();
    return m_scheduler->submitTask(parentTaskHandles, parentTaskHandleCount, runTask, task);
}

void TaskPool::releaseTask(TaskHandle taskHandle)
{
    m_scheduler->releaseTask(taskHandle);
}

void TaskPool::waitForCompletion(TaskHandle taskHandle)
{
    m_scheduler->waitForCompletion(taskHandle);
}

void TaskPool::waitForCompletion(TaskHandle* taskHandles, uint32_t taskHandleCount)
{
    if (taskHandles && taskHandleCount > 0)
    {
        RefPtr<WaitTask> waitTask = new WaitTask();
        TaskHandle waitTaskHandle = submitTask(waitTask, taskHandles, taskHandleCount);
        waitForCompletion(waitTaskHandle);
        releaseTask(waitTaskHandle);
    }
}

static std::mutex s_globalTaskPoolMutex;
static std::unique_ptr<TaskPool> s_globalTaskPool;
static uint32_t s_globalTaskPoolWorkerCount = TaskPool::kAutoWorkerCount;
static ComPtr<ITaskScheduler> s_globalTaskScheduler;

Result setGlobalTaskPoolWorkerCount(uint32_t count)
{
    std::lock_guard<std::mutex> lock(s_globalTaskPoolMutex);
    if (s_globalTaskPool)
        return SLANG_FAIL;
    s_globalTaskPoolWorkerCount = count;
    return SLANG_OK;
}

Result setGlobalTaskScheduler(ITaskScheduler* scheduler)
{
    std::lock_guard<std::mutex> lock(s_globalTaskPoolMutex);
    if (s_globalTaskPool)
        return SLANG_FAIL;
    s_globalTaskScheduler = scheduler;
    return SLANG_OK;
}

TaskPool& globalTaskPool()
{
    static std::atomic<TaskPool*> taskPoolPtr;
    if (taskPoolPtr)
    {
        return *taskPoolPtr;
    }
    std::lock_guard<std::mutex> lock(s_globalTaskPoolMutex);
    if (!s_globalTaskPool)
    {
        s_globalTaskPool.reset(
            s_globalTaskScheduler ? new TaskPool(s_globalTaskScheduler) : new TaskPool(s_globalTaskPoolWorkerCount)
        );
        taskPoolPtr = s_globalTaskPool.get();
    }
    return *taskPoolPtr;
}

} // namespace rhi
