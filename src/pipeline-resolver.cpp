#include "pipeline-resolver.h"

#include "command-list.h"
#include "core/task-pool.h"
#include "device.h"
#include "pipeline.h"
#include "shader.h"
#include "shader-object.h"

#include <unordered_map>

namespace rhi {

namespace {

class TaskBatch
{
public:
    explicit TaskBatch(ITaskPool* taskPool)
        : m_taskPool(taskPool)
    {
    }

    ~TaskBatch() { wait(); }

    Result submit(void (*func)(void*), void* payload, void (*payloadDeleter)(void*))
    {
        auto handle = m_taskPool->submitTask(func, payload, payloadDeleter, nullptr, 0);
        SLANG_RHI_ASSERT(handle);
        if (!handle)
        {
            if (payloadDeleter)
                payloadDeleter(payload);
            return SLANG_FAIL;
        }
        m_handles.push_back(handle);
        return SLANG_OK;
    }

    void wait()
    {
        for (auto handle : m_handles)
        {
            m_taskPool->waitTask(handle);
            m_taskPool->releaseTask(handle);
        }
        m_handles.clear();
    }

private:
    ITaskPool* m_taskPool;
    std::vector<ITaskPool::TaskHandle> m_handles;
};

struct PipelineKeyHasher
{
    size_t operator()(const PipelineKey& key) const { return key.hash; }
};

struct PipelineRequest
{
    PipelineKey key = {};
    RefPtr<Pipeline> pipeline;
    ExtendedShaderObjectTypeListObject* specializationArgs = nullptr;
    std::vector<const CommandList::CommandSlot*> commands;

    RefPtr<ShaderProgram> program;
    RefPtr<Pipeline> concretePipeline;
    Result result = SLANG_OK;
    bool created = false;
};

struct ProgramWork
{
    RefPtr<ShaderProgram> program;
    std::unique_lock<std::mutex> compileLock;
    std::vector<CompiledEntryPoint> entryPoints;

    explicit ProgramWork(ShaderProgram* program_)
        : program(program_)
        , compileLock(program_->m_compileMutex)
    {
    }

    ProgramWork(ProgramWork&&) = default;
    ProgramWork& operator=(ProgramWork&&) = default;
    ProgramWork(const ProgramWork&) = delete;
    ProgramWork& operator=(const ProgramWork&) = delete;
};

class PipelineResolver
{
public:
    PipelineResolver(Device* device, CommandList* commandList)
        : m_device(device)
        , m_commandList(commandList)
    {
    }

    Result resolve()
    {
        std::lock_guard<std::mutex> resolutionLock(m_device->m_pipelineResolutionMutex);

        if (m_device->m_pipelineCompilationMode == PipelineCompilationMode::Serial ||
            m_device->getInfo().deviceType == DeviceType::CPU)
        {
            return resolveSerial();
        }

        SLANG_RETURN_ON_FAIL(collectRequests());
        SLANG_RETURN_ON_FAIL(preparePrograms());
        SLANG_RETURN_ON_FAIL(compilePrograms());
        SLANG_RETURN_ON_FAIL(createPipelines());
        finalize();
        return SLANG_OK;
    }

private:
    static void getCommandPipeline(
        CommandList* commandList,
        const CommandList::CommandSlot* command,
        Pipeline*& outPipeline,
        ExtendedShaderObjectTypeListObject*& outSpecializationArgs
    )
    {
        outPipeline = nullptr;
        outSpecializationArgs = nullptr;
        switch (command->id)
        {
        case CommandID::SetRenderState:
        {
            auto& cmd = commandList->getCommand<commands::SetRenderState>(command);
            outPipeline = checked_cast<RenderPipeline*>(cmd.pipeline);
            outSpecializationArgs = static_cast<ExtendedShaderObjectTypeListObject*>(cmd.specializationArgs);
            break;
        }
        case CommandID::SetComputeState:
        {
            auto& cmd = commandList->getCommand<commands::SetComputeState>(command);
            outPipeline = checked_cast<ComputePipeline*>(cmd.pipeline);
            outSpecializationArgs = static_cast<ExtendedShaderObjectTypeListObject*>(cmd.specializationArgs);
            break;
        }
        case CommandID::SetRayTracingState:
        {
            auto& cmd = commandList->getCommand<commands::SetRayTracingState>(command);
            outPipeline = checked_cast<RayTracingPipeline*>(cmd.pipeline);
            outSpecializationArgs = static_cast<ExtendedShaderObjectTypeListObject*>(cmd.specializationArgs);
            break;
        }
        default:
            break;
        }
    }

    static void patchCommand(
        CommandList* commandList,
        const CommandList::CommandSlot* command,
        Pipeline* concretePipeline
    )
    {
        switch (command->id)
        {
        case CommandID::SetRenderState:
        {
            auto& cmd = commandList->getCommand<commands::SetRenderState>(command);
            cmd.pipeline = checked_cast<RenderPipeline*>(concretePipeline);
            cmd.specializationArgs = nullptr;
            break;
        }
        case CommandID::SetComputeState:
        {
            auto& cmd = commandList->getCommand<commands::SetComputeState>(command);
            cmd.pipeline = checked_cast<ComputePipeline*>(concretePipeline);
            cmd.specializationArgs = nullptr;
            break;
        }
        case CommandID::SetRayTracingState:
        {
            auto& cmd = commandList->getCommand<commands::SetRayTracingState>(command);
            cmd.pipeline = checked_cast<RayTracingPipeline*>(concretePipeline);
            cmd.specializationArgs = nullptr;
            break;
        }
        default:
            break;
        }
    }

    Result resolveSerial()
    {
        for (auto command = m_commandList->getCommands(); command; command = command->next)
        {
            Pipeline* pipeline;
            ExtendedShaderObjectTypeListObject* specializationArgs;
            getCommandPipeline(m_commandList, command, pipeline, specializationArgs);
            if (!pipeline)
                continue;

            Pipeline* concretePipeline = nullptr;
            SLANG_RETURN_ON_FAIL(m_device->getConcretePipeline(pipeline, specializationArgs, concretePipeline));
            patchCommand(m_commandList, command, concretePipeline);
        }
        return SLANG_OK;
    }

    Result collectRequests()
    {
        std::unordered_map<PipelineKey, size_t, PipelineKeyHasher> requestMap;

        for (auto command = m_commandList->getCommands(); command; command = command->next)
        {
            Pipeline* pipeline;
            ExtendedShaderObjectTypeListObject* specializationArgs;
            getCommandPipeline(m_commandList, command, pipeline, specializationArgs);
            if (!pipeline)
                continue;

            if (!pipeline->isVirtual())
            {
                patchCommand(m_commandList, command, pipeline);
                continue;
            }

            PipelineKey key = {};
            key.pipeline = pipeline;
            if (pipeline->m_program->isSpecializable())
            {
                if (!specializationArgs)
                    return SLANG_FAIL;
                for (ShaderComponentID componentID : specializationArgs->componentIDs)
                    key.specializationArgs.push_back(componentID);
            }
            key.updateHash();

            auto [it, inserted] = requestMap.emplace(key, m_requests.size());
            if (inserted)
            {
                PipelineRequest request;
                request.key = key;
                request.pipeline = pipeline;
                request.specializationArgs = specializationArgs;
                request.commands.push_back(command);

                if (pipeline->m_program->isSpecializable())
                    request.concretePipeline = m_device->m_shaderCache.getSpecializedPipeline(key);
                else
                    request.concretePipeline = pipeline->getConcretePipeline();

                m_requests.push_back(std::move(request));
            }
            else
            {
                m_requests[it->second].commands.push_back(command);
            }
        }
        return SLANG_OK;
    }

    Result preparePrograms()
    {
        std::unordered_map<ShaderProgram*, size_t> programMap;

        for (auto& request : m_requests)
        {
            if (request.concretePipeline)
                continue;

            request.program = request.pipeline->m_program;
            if (request.program->isSpecializable())
            {
                RefPtr<ShaderProgram> specializedProgram;
                SLANG_RETURN_ON_FAIL(m_device->getSpecializedProgram(
                    request.program,
                    *request.specializationArgs,
                    specializedProgram.writeRef()
                ));
                request.program = specializedProgram;
            }

            if (programMap.find(request.program) == programMap.end())
            {
                size_t index = m_programs.size();
                programMap.emplace(request.program, index);
                m_programs.emplace_back(request.program);
                ProgramWork& programWork = m_programs.back();
                if (!programWork.program->m_compiledShaders)
                {
                    SLANG_RETURN_ON_FAIL(
                        programWork.program->prepareEntryPointCompilation(m_device, programWork.entryPoints)
                    );
                }
            }
        }
        return SLANG_OK;
    }

    Result compilePrograms()
    {
        size_t entryPointCount = 0;
        for (const auto& program : m_programs)
            entryPointCount += program.entryPoints.size();

        if (entryPointCount > 1)
        {
            struct Payload
            {
                Device* device;
                ShaderProgram* program;
                CompiledEntryPoint* entryPoint;
            };

            TaskBatch batch(globalTaskPool());
            for (auto& program : m_programs)
            {
                for (auto& entryPoint : program.entryPoints)
                {
                    auto* payload = new Payload{m_device, program.program, &entryPoint};
                    SLANG_RETURN_ON_FAIL(batch.submit(
                        [](void* data)
                        {
                            auto* payload = static_cast<Payload*>(data);
                            // TODO: Slang exposes only cumulative global compiler timing. Parallel
                            // entry-point compilation therefore cannot currently attribute compiler
                            // time accurately to individual compilation reports.
                            payload->entryPoint->result =
                                payload->program->compileEntryPoint(payload->device, *payload->entryPoint, false);
                        },
                        payload,
                        [](void* data)
                        {
                            delete static_cast<Payload*>(data);
                        }
                    ));
                }
            }
            batch.wait();
        }
        else
        {
            for (auto& program : m_programs)
            {
                for (auto& entryPoint : program.entryPoints)
                {
                    entryPoint.result = program.program->compileEntryPoint(m_device, entryPoint, true);
                }
            }
        }

        Result firstFailure = SLANG_OK;
        for (auto& program : m_programs)
        {
            for (const auto& entryPoint : program.entryPoints)
            {
                program.program->reportEntryPointCompilation(m_device, entryPoint);
                if (SLANG_FAILED(entryPoint.result) && SLANG_SUCCEEDED(firstFailure))
                    firstFailure = entryPoint.result;
            }
        }
        SLANG_RETURN_ON_FAIL(firstFailure);

        // Backend module containers are populated serially. Pipeline workers only read them afterwards.
        for (auto& program : m_programs)
        {
            if (!program.entryPoints.empty())
                SLANG_RETURN_ON_FAIL(program.program->installCompiledEntryPoints(program.entryPoints));
            program.compileLock.unlock();
        }
        return SLANG_OK;
    }

    static void createPipelineTask(void* data)
    {
        auto* payload = static_cast<std::pair<Device*, PipelineRequest*>*>(data);
        Device* device = payload->first;
        PipelineRequest* request = payload->second;

        request->result = device->pushCudaContext();
        if (SLANG_FAILED(request->result))
            return;

        request->result =
            device->createConcretePipeline(request->pipeline, request->program, request->concretePipeline);
        Result popResult = device->popCudaContext();
        if (SLANG_SUCCEEDED(request->result))
            request->result = popResult;
    }

    Result createPipelines()
    {
        std::vector<PipelineRequest*> workerRequests;
        std::vector<PipelineRequest*> callerRequests;
        for (auto& request : m_requests)
        {
            if (!request.concretePipeline)
            {
                if (m_device->canCreatePipelineOnTaskPool(request.pipeline))
                    workerRequests.push_back(&request);
                else
                    callerRequests.push_back(&request);
            }
        }

        if (workerRequests.size() > 1)
        {
            TaskBatch batch(globalTaskPool());
            for (PipelineRequest* request : workerRequests)
            {
                request->created = true;
                auto* payload = new std::pair<Device*, PipelineRequest*>(m_device, request);
                SLANG_RETURN_ON_FAIL(batch.submit(
                    createPipelineTask,
                    payload,
                    [](void* data)
                    {
                        delete static_cast<std::pair<Device*, PipelineRequest*>*>(data);
                    }
                ));
            }
            batch.wait();
        }
        else
        {
            for (PipelineRequest* request : workerRequests)
            {
                request->created = true;
                std::pair<Device*, PipelineRequest*> payload(m_device, request);
                createPipelineTask(&payload);
            }
        }

        // Caller-only requests run after task-pool work completes. In particular, this
        // prevents CUDA ray-tracing pipeline workers from blocking the same pool used by OptiX.
        for (PipelineRequest* request : callerRequests)
        {
            request->created = true;
            std::pair<Device*, PipelineRequest*> payload(m_device, request);
            createPipelineTask(&payload);
        }

        for (const auto& request : m_requests)
            SLANG_RETURN_ON_FAIL(request.result);
        return SLANG_OK;
    }

    void finalize()
    {
        for (auto& request : m_requests)
        {
            if (request.created)
            {
                if (request.pipeline->m_program->isSpecializable())
                {
                    m_device->m_shaderCache.addSpecializedPipeline(request.key, request.concretePipeline);
                    request.concretePipeline->breakStrongReferenceToDevice();
                    request.concretePipeline->m_program->breakStrongReferenceToDevice();
                }
                else
                {
                    request.pipeline->setConcretePipeline(request.concretePipeline);
                }
            }

            for (const auto* command : request.commands)
                patchCommand(m_commandList, command, request.concretePipeline);
        }
    }

    Device* m_device;
    CommandList* m_commandList;
    std::vector<PipelineRequest> m_requests;
    std::vector<ProgramWork> m_programs;
};

} // namespace

Result resolvePipelines(Device* device, CommandList* commandList)
{
    PipelineResolver resolver(device, commandList);
    return resolver.resolve();
}

} // namespace rhi
