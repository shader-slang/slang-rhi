#include "cpu-command.h"
#include "cpu-query.h"
#include "cpu-shader-program.h"
#include "cpu-pipeline.h"
#include "../command-list.h"
#include "../strings.h"

#include "core/platform.h"
#include "core/task-pool.h"

#include <algorithm>
#include <thread>
#include <vector>

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

void CommandExecutor::cmdDispatchCompute(const commands::DispatchCompute& cmd)
{
    if (!m_computeStateValid)
        return;

    if (cmd.x == 0 || cmd.y == 0 || cmd.z == 0)
        return;

    // A slice of the dispatch: the kernel's group-range function runs every group in
    // [startGroupID, endGroupID) itself.
    struct Slice
    {
        slang_prelude::ComputeFunc func;
        slang_prelude::ComputeVaryingInput varyingInput;
        void* entryPointData;
        void* globalData;

        static void run(void* payload)
        {
            Slice* slice = static_cast<Slice*>(payload);
            slice->func(&slice->varyingInput, slice->entryPointData, slice->globalData);
        }
    };

    Slice whole = {};
    whole.func = m_computePipeline->m_func;
    whole.varyingInput.startGroupID.x = 0;
    whole.varyingInput.startGroupID.y = 0;
    whole.varyingInput.startGroupID.z = 0;
    whole.varyingInput.endGroupID.x = cmd.x;
    whole.varyingInput.endGroupID.y = cmd.y;
    whole.varyingInput.endGroupID.z = cmd.z;
    whole.entryPointData = m_bindingData->entryPoints[0].data;
    whole.globalData = m_bindingData->globalData;

    // Thread groups are independent of each other, so the dispatch is split into slices that
    // run on the global task pool. The split is along the axis with the most groups, so that
    // every slice is again a box of groups. A few slices per hardware thread let the pool
    // balance groups of uneven cost.
    uint32_t groupCount[3] = {cmd.x, cmd.y, cmd.z};
    int axis = 0;
    for (int i = 1; i < 3; ++i)
    {
        if (groupCount[i] > groupCount[axis])
            axis = i;
    }
    uint32_t extent = groupCount[axis];
    uint32_t threadCount = std::max(1u, std::thread::hardware_concurrency());
    uint32_t sliceCount = std::min(extent, 4 * threadCount);
    if (sliceCount <= 1)
    {
        Slice::run(&whole);
        return;
    }

    std::vector<Slice> slices(sliceCount, whole);
    ITaskPool* taskPool = globalTaskPool();
    ITaskPool::TaskGroupHandle taskGroup = taskPool->createTaskGroup();
    for (uint32_t i = 0; i < sliceCount; ++i)
    {
        slang_prelude::ComputeVaryingInput& input = slices[i].varyingInput;
        uint32_t begin = uint32_t(uint64_t(extent) * i / sliceCount);
        uint32_t end = uint32_t(uint64_t(extent) * (i + 1) / sliceCount);
        (axis == 0 ? input.startGroupID.x : axis == 1 ? input.startGroupID.y : input.startGroupID.z) = begin;
        (axis == 0 ? input.endGroupID.x : axis == 1 ? input.endGroupID.y : input.endGroupID.z) = end;
        taskPool->releaseTask(taskPool->submitTask(Slice::run, &slices[i], nullptr, taskGroup));
    }
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
