#include "cpu-command.h"
#include "cpu-query.h"
#include "cpu-shader-program.h"
#include "cpu-pipeline.h"
#include "../command-list.h"
#include "../strings.h"

#include "core/platform.h"
#include "core/task-pool.h"

#include <algorithm>
#include <atomic>
#include <thread>

namespace rhi::cpu {

class CommandExecutor
{
public:
    DeviceImpl* m_device;
    uint64_t m_submissionID;
    RefPtr<ComputePipelineImpl> m_computePipeline;
    BindingDataImpl* m_bindingData = nullptr;
    bool m_computeStateValid = false;

    CommandExecutor(DeviceImpl* device, uint64_t submissionID)
        : m_device(device)
        , m_submissionID(submissionID)
    {
    }

    Result execute(CommandBufferImpl* commandBuffer);

    void cmdCopyBuffer(const commands::CopyBuffer& cmd);
    void cmdCopyTexture(const commands::CopyTexture& cmd);
    void cmdCopyTextureToBuffer(const commands::CopyTextureToBuffer& cmd);
    void cmdClearBuffer(const commands::ClearBuffer& cmd);
    void cmdClearTextureFloat(const commands::ClearTextureFloat& cmd);
    void cmdClearTextureUint(const commands::ClearTextureUint& cmd);
    void cmdClearTextureDepthStencil(const commands::ClearTextureDepthStencil& cmd);
    void cmdUploadTextureData(const commands::UploadTextureData& cmd);
    void cmdResolveQuery(const commands::ResolveQuery& cmd);
    void cmdBeginRenderPass(const commands::BeginRenderPass& cmd);
    void cmdEndRenderPass(const commands::EndRenderPass& cmd);
    void cmdSetRenderState(const commands::SetRenderState& cmd);
    void cmdDraw(const commands::Draw& cmd);
    void cmdDrawIndexed(const commands::DrawIndexed& cmd);
    void cmdDrawIndirect(const commands::DrawIndirect& cmd);
    void cmdDrawIndexedIndirect(const commands::DrawIndexedIndirect& cmd);
    void cmdDrawMeshTasks(const commands::DrawMeshTasks& cmd);
    void cmdBeginComputePass(const commands::BeginComputePass& cmd);
    void cmdEndComputePass(const commands::EndComputePass& cmd);
    void cmdSetComputeState(const commands::SetComputeState& cmd);
    void cmdDispatchCompute(const commands::DispatchCompute& cmd);
    void cmdDispatchComputeIndirect(const commands::DispatchComputeIndirect& cmd);
    void cmdBeginRayTracingPass(const commands::BeginRayTracingPass& cmd);
    void cmdEndRayTracingPass(const commands::EndRayTracingPass& cmd);
    void cmdSetRayTracingState(const commands::SetRayTracingState& cmd);
    void cmdDispatchRays(const commands::DispatchRays& cmd);
    void cmdBuildAccelerationStructure(const commands::BuildAccelerationStructure& cmd);
    void cmdBuildMicromap(const commands::BuildMicromap& cmd);
    void cmdCopyAccelerationStructure(const commands::CopyAccelerationStructure& cmd);
    void cmdQueryAccelerationStructureProperties(const commands::QueryAccelerationStructureProperties& cmd);
    void cmdExecuteClusterOperation(const commands::ExecuteClusterOperation& cmd);
    void cmdConvertCooperativeVectorMatrix(const commands::ConvertCooperativeVectorMatrix& cmd);
    void cmdSetBufferState(const commands::SetBufferState& cmd);
    void cmdSetTextureState(const commands::SetTextureState& cmd);
    void cmdGlobalBarrier(const commands::GlobalBarrier& cmd);
    void cmdAliasResources(const commands::AliasResources& cmd);
    void cmdPushDebugGroup(const commands::PushDebugGroup& cmd);
    void cmdPopDebugGroup(const commands::PopDebugGroup& cmd);
    void cmdInsertDebugMarker(const commands::InsertDebugMarker& cmd);
    void cmdWriteTimestamp(const commands::WriteTimestamp& cmd);
    void cmdExecuteCallback(const commands::ExecuteCallback& cmd);
};

Result CommandExecutor::execute(CommandBufferImpl* commandBuffer)
{
    CommandList& commandList = commandBuffer->m_commandList;
    auto command = commandList.getCommands();
    while (command)
    {
#define SLANG_RHI_COMMAND_EXECUTE_X(x)                                                                                 \
    case CommandID::x:                                                                                                 \
        cmd##x(commandList.getCommand<commands::x>(command));                                                          \
        break;

        switch (command->id)
        {
            SLANG_RHI_COMMANDS(SLANG_RHI_COMMAND_EXECUTE_X);
        }

#undef SLANG_RHI_COMMAND_EXECUTE_X

        command = command->next;
    }

    return SLANG_OK;
}

#define NOT_SUPPORTED(interface, method) m_device->printWarning(#interface "::" #method " is not supported!")

void CommandExecutor::cmdCopyBuffer(const commands::CopyBuffer& cmd)
{
    BufferImpl* dst = checked_cast<BufferImpl*>(cmd.dst);
    BufferImpl* src = checked_cast<BufferImpl*>(cmd.src);
    std::memcpy(dst->m_data + cmd.dstOffset, src->m_data + cmd.srcOffset, cmd.size);
}

void CommandExecutor::cmdCopyTexture(const commands::CopyTexture& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(ICommandEncoder, copyTexture);
}

void CommandExecutor::cmdCopyTextureToBuffer(const commands::CopyTextureToBuffer& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(ICommandEncoder, copyTextureToBuffer);
}

void CommandExecutor::cmdClearBuffer(const commands::ClearBuffer& cmd)
{
    BufferImpl* buffer = checked_cast<BufferImpl*>(cmd.buffer);
    std::memset(buffer->m_data + cmd.range.offset, 0, cmd.range.size);
}

void CommandExecutor::cmdClearTextureDepthStencil(const commands::ClearTextureDepthStencil& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(ICommandEncoder, clearTextureDepthStencil);
}

void CommandExecutor::cmdClearTextureFloat(const commands::ClearTextureFloat& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(ICommandEncoder, clearTextureFloat);
}

void CommandExecutor::cmdClearTextureUint(const commands::ClearTextureUint& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(ICommandEncoder, clearTextureUint);
}

void CommandExecutor::cmdUploadTextureData(const commands::UploadTextureData& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(ICommandEncoder, uploadTextureData);
}

void CommandExecutor::cmdResolveQuery(const commands::ResolveQuery& cmd)
{
    BufferImpl* buffer = checked_cast<BufferImpl*>(cmd.buffer);
    QueryPoolImpl* queryPool = checked_cast<QueryPoolImpl*>(cmd.queryPool);
    std::memcpy(buffer->m_data + cmd.offset, queryPool->m_queries.data() + cmd.index, cmd.count * sizeof(uint64_t));
}

void CommandExecutor::cmdBeginRenderPass(const commands::BeginRenderPass& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(ICommandEncoder, beginRenderPass);
}

void CommandExecutor::cmdEndRenderPass(const commands::EndRenderPass& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdSetRenderState(const commands::SetRenderState& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdDraw(const commands::Draw& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(IRenderPassEncoder, draw);
}

void CommandExecutor::cmdDrawIndexed(const commands::DrawIndexed& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(IRenderPassEncoder, drawIndexed);
}

void CommandExecutor::cmdDrawIndirect(const commands::DrawIndirect& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(IRenderPassEncoder, drawIndirect);
}

void CommandExecutor::cmdDrawIndexedIndirect(const commands::DrawIndexedIndirect& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(IRenderPassEncoder, drawIndexedIndirect);
}

void CommandExecutor::cmdDrawMeshTasks(const commands::DrawMeshTasks& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(IRenderPassEncoder, drawMeshTasks);
}

void CommandExecutor::cmdBeginComputePass(const commands::BeginComputePass& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdEndComputePass(const commands::EndComputePass& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdSetComputeState(const commands::SetComputeState& cmd)
{
    m_computePipeline = checked_cast<ComputePipelineImpl*>(cmd.pipeline);
    m_bindingData = static_cast<BindingDataImpl*>(cmd.bindingData);
    m_computeStateValid = m_computePipeline && m_bindingData;
}

// Fewest threads a dispatch slice gets. Running a slice on the pool costs a task submission and a
// worker wake-up, so for a trivial kernel splitting only pays off from about 2 x 8192 threads.
// Measured on a 12-core/24-thread CPU: smaller dispatches run inline at the serial speed, and
// larger ones run 1.4-2x (32K threads) to 6-8x (1M threads) faster than serially.
static constexpr uint64_t kMinThreadsPerSlice = 8192;

// Worker threads of a task pool that can run dispatch slices next to the submitting thread: the
// pool's worker count when slang-rhi created it (0 for a BlockingTaskPool, so every slice runs
// inline). A pool set with setGlobalTaskPool does not report its size; assume one worker per
// hardware thread, capped at kMaxAssumedWorkerCount so that an unknown pool on a large machine
// (or in a container with few CPUs) is not flooded with tasks that only find the slices taken.
static uint32_t getDispatchWorkerCount(ITaskPool* taskPool)
{
    static constexpr uint32_t kMaxAssumedWorkerCount = 8;
    int workerCount = getTaskPoolWorkerCount(taskPool);
    if (workerCount >= 0)
        return uint32_t(workerCount);
    return std::min(std::max(1u, std::thread::hardware_concurrency()), kMaxAssumedWorkerCount);
}

void CommandExecutor::cmdDispatchCompute(const commands::DispatchCompute& cmd)
{
    if (!m_computeStateValid)
        return;

    if (cmd.x == 0 || cmd.y == 0 || cmd.z == 0)
        return;

    // The whole dispatch, shared by every thread that runs part of it. The kernel's group-range
    // function runs every group in [startGroupID, endGroupID) itself, so a slice only narrows
    // that box along one axis. Threads claim slices from an atomic counter.
    struct DispatchContext
    {
        slang_prelude::ComputeFunc func;
        slang_prelude::ComputeVaryingInput whole;
        void* entryPointData;
        void* globalData;
        int axis;
        uint32_t extent;
        uint32_t sliceCount;
        std::atomic<uint32_t> nextSlice{0};

        void runSlice(uint32_t i)
        {
            slang_prelude::ComputeVaryingInput input = whole;
            uint32_t* start = &input.startGroupID.x;
            uint32_t* end = &input.endGroupID.x;
            start[axis] = uint32_t(uint64_t(extent) * i / sliceCount);
            end[axis] = uint32_t(uint64_t(extent) * (i + 1) / sliceCount);
            func(&input, entryPointData, globalData);
        }

        static void run(void* payload)
        {
            DispatchContext* ctx = static_cast<DispatchContext*>(payload);
            for (uint32_t i; (i = ctx->nextSlice.fetch_add(1, std::memory_order_relaxed)) < ctx->sliceCount;)
                ctx->runSlice(i);
        }
    };

    DispatchContext ctx;
    ctx.func = m_computePipeline->m_func;
    ctx.whole.startGroupID.x = 0;
    ctx.whole.startGroupID.y = 0;
    ctx.whole.startGroupID.z = 0;
    ctx.whole.endGroupID.x = cmd.x;
    ctx.whole.endGroupID.y = cmd.y;
    ctx.whole.endGroupID.z = cmd.z;
    ctx.entryPointData = m_bindingData->entryPoints[0].data;
    ctx.globalData = m_bindingData->globalData;

    // Thread groups are independent of each other, so a large dispatch is split into slices that
    // run on the global task pool. The split is along the axis with the most groups, so that
    // every slice is again a box of groups. A few slices per runner let the threads balance
    // groups of uneven cost.
    uint32_t groupCount[3] = {cmd.x, cmd.y, cmd.z};
    ctx.axis = 0;
    for (int i = 1; i < 3; ++i)
    {
        if (groupCount[i] > groupCount[ctx.axis])
            ctx.axis = i;
    }
    ctx.extent = groupCount[ctx.axis];

    // Slices run on the pool's workers and on the submitting thread. Each slice gets at least
    // kMinThreadsPerSlice threads, because submitting pool tasks and waiting for them costs more
    // than a smaller slice saves. A small dispatch, or one without pool workers, runs inline.
    uint64_t threadTotal = uint64_t(cmd.x) * cmd.y * cmd.z * m_computePipeline->m_threadGroupThreadCount;
    uint64_t sliceCount = std::min<uint64_t>(ctx.extent, threadTotal / kMinThreadsPerSlice);
    ITaskPool* taskPool = sliceCount > 1 ? globalTaskPool() : nullptr;
    uint32_t workerCount = taskPool ? getDispatchWorkerCount(taskPool) : 0;
    if (workerCount == 0)
    {
        ctx.func(&ctx.whole, ctx.entryPointData, ctx.globalData);
        return;
    }
    ctx.sliceCount = uint32_t(std::min<uint64_t>(sliceCount, 4 * uint64_t(workerCount + 1)));

    // At most one pool task per worker; the submitting thread runs slices too. The context lives
    // on this stack frame, so wait for the tasks before returning.
    uint32_t taskCount = std::min(ctx.sliceCount - 1, workerCount);
    ITaskPool::TaskGroupHandle taskGroup = taskPool->createTaskGroup();
    for (uint32_t i = 0; i < taskCount; ++i)
        taskPool->releaseTask(taskPool->submitTask(DispatchContext::run, &ctx, nullptr, taskGroup));
    DispatchContext::run(&ctx);
    taskPool->waitAndReleaseTaskGroup(taskGroup);
}

void CommandExecutor::cmdDispatchComputeIndirect(const commands::DispatchComputeIndirect& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(IComputePassEncoder, dispatchComputeIndirect);
}

void CommandExecutor::cmdBeginRayTracingPass(const commands::BeginRayTracingPass& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(ICommandEncoder, beginRayTracingPass);
}

void CommandExecutor::cmdEndRayTracingPass(const commands::EndRayTracingPass& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdSetRayTracingState(const commands::SetRayTracingState& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdDispatchRays(const commands::DispatchRays& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(IRayTracingPassEncoder, dispatchRays);
}

void CommandExecutor::cmdBuildAccelerationStructure(const commands::BuildAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(ICommandEncoder, buildAccelerationStructure);
}

void CommandExecutor::cmdBuildMicromap(const commands::BuildMicromap& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(ICommandEncoder, buildMicromap);
}

void CommandExecutor::cmdCopyAccelerationStructure(const commands::CopyAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(ICommandEncoder, copyAccelerationStructure);
}

void CommandExecutor::cmdQueryAccelerationStructureProperties(const commands::QueryAccelerationStructureProperties& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(ICommandEncoder, queryAccelerationStructureProperties);
}

void CommandExecutor::cmdExecuteClusterOperation(const commands::ExecuteClusterOperation& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(ICommandEncoder, executeClusterOperation);
}

void CommandExecutor::cmdConvertCooperativeVectorMatrix(const commands::ConvertCooperativeVectorMatrix& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(ICommandEncoder, convertCooperativeVectorMatrix);
}

void CommandExecutor::cmdSetBufferState(const commands::SetBufferState& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdSetTextureState(const commands::SetTextureState& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdGlobalBarrier(const commands::GlobalBarrier& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdAliasResources(const commands::AliasResources& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdPushDebugGroup(const commands::PushDebugGroup& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdPopDebugGroup(const commands::PopDebugGroup& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdInsertDebugMarker(const commands::InsertDebugMarker& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdWriteTimestamp(const commands::WriteTimestamp& cmd)
{
    auto queryPool = checked_cast<QueryPoolImpl*>(cmd.queryPool);
    queryPool->m_queries[cmd.queryIndex] = getCpuTimestamp();
    queryPool->markQueryRangeSubmitted(cmd.queryIndex, 1, m_submissionID);
    queryPool->markQueryRangeResolved(cmd.queryIndex, 1, m_submissionID);
}

void CommandExecutor::cmdExecuteCallback(const commands::ExecuteCallback& cmd)
{
    invokeExecuteCallback(cmd, {});
}

// CommandQueueImpl

CommandQueueImpl::CommandQueueImpl(Device* device, QueueType type)
    : CommandQueue(device, type)
{
}

Result CommandQueueImpl::createCommandEncoder(const CommandEncoderDesc& desc, ICommandEncoder** outEncoder)
{
    RefPtr<CommandEncoderImpl> encoder = new CommandEncoderImpl(m_device, desc);
    SLANG_RETURN_ON_FAIL(encoder->init());
    returnComPtr(outEncoder, encoder);
    return SLANG_OK;
}

Result CommandQueueImpl::submit(const SubmitDesc& desc)
{
    // Wait for fences.
    for (uint32_t i = 0; i < desc.waitFenceCount; ++i)
    {
        // TODO: wait for fence
        uint64_t fenceValue;
        SLANG_RETURN_ON_FAIL(desc.waitFences[i]->getCurrentValue(&fenceValue));
        if (fenceValue < desc.waitFenceValues[i])
        {
            return SLANG_FAIL;
        }
    }

    ++m_lastSubmittedID;

    // Execute command buffers.
    for (uint32_t i = 0; i < desc.commandBufferCount; i++)
    {
        CommandExecutor executor(getDevice<DeviceImpl>(), m_lastSubmittedID);
        SLANG_RETURN_ON_FAIL(executor.execute(checked_cast<CommandBufferImpl*>(desc.commandBuffers[i])));
    }

    // Signal fences.
    for (uint32_t i = 0; i < desc.signalFenceCount; ++i)
    {
        SLANG_RETURN_ON_FAIL(desc.signalFences[i]->setCurrentValue(desc.signalFenceValues[i]));
    }

    return SLANG_OK;
}

Result CommandQueueImpl::waitOnHost()
{
    return SLANG_OK;
}

Result CommandQueueImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result CommandQueueImpl::getTimestampCalibration(TimestampCalibration* outCalibration)
{
    if (!outCalibration)
    {
        return SLANG_E_INVALID_ARG;
    }

    const uint64_t cpuFrequency = getCpuTimestampFrequency();
    const uint64_t cpuTimestamp = getCpuTimestamp();

    outCalibration->cpuDomain = getCpuTimestampDomain();
    outCalibration->cpuTimestamp = cpuTimestamp;
    outCalibration->cpuFrequency = cpuFrequency;
    outCalibration->gpuTimestamp = cpuTimestamp;
    outCalibration->gpuFrequency = cpuFrequency;
    outCalibration->maxDeviationNs = 0;

    return SLANG_OK;
}

// CommandEncoderImpl

CommandEncoderImpl::CommandEncoderImpl(Device* device, const CommandEncoderDesc& desc)
    : CommandEncoder(device, desc)
{
}

Result CommandEncoderImpl::getBindingData(RootShaderObject* rootObject, BindingData*& outBindingData)
{
    rootObject->trackResources(m_commandBuffer->m_trackedObjects);
    BindingDataBuilder builder;
    builder.m_device = getDevice<DeviceImpl>();
    builder.m_bindingCache = &m_commandBuffer->m_bindingCache;
    builder.m_allocator = &m_commandBuffer->m_allocator;
    ShaderObjectLayout* specializedLayout = nullptr;
    SLANG_RETURN_ON_FAIL(rootObject->getSpecializedLayout(specializedLayout));
    return builder.bindAsRoot(
        rootObject,
        checked_cast<RootShaderObjectLayoutImpl*>(specializedLayout),
        (BindingDataImpl*&)outBindingData
    );
}

Result CommandEncoderImpl::init()
{
    m_commandBuffer = new CommandBufferImpl(m_device);
    m_commandList = &m_commandBuffer->m_commandList;
    return SLANG_OK;
}

Result CommandEncoderImpl::finish(const CommandBufferDesc& desc, ICommandBuffer** outCommandBuffer)
{
    m_commandBuffer->setDesc(desc);
    SLANG_RETURN_ON_FAIL(resolvePipelines(m_device));
    returnComPtr(outCommandBuffer, m_commandBuffer);
    m_commandList = nullptr;
    return SLANG_OK;
}

Result CommandEncoderImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

// CommandBufferImpl

CommandBufferImpl::CommandBufferImpl(Device* device)
    : CommandBuffer(device)
{
}

Result CommandBufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

} // namespace rhi::cpu
