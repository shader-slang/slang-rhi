#include "cpu-command.h"
#include "cpu-query.h"
#include "cpu-shader-program.h"
#include "cpu-pipeline.h"
#include "../command-list.h"
#include "../strings.h"

#include <chrono>

namespace rhi::cpu {

class CommandExecutor
{
public:
    DeviceImpl* m_device;
    RefPtr<ComputePipelineImpl> m_computePipeline;
    BindingDataImpl* m_bindingData = nullptr;
    bool m_computeStateValid = false;

    CommandExecutor(DeviceImpl* device)
        : m_device(device)
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
    void cmdCopyAccelerationStructure(const commands::CopyAccelerationStructure& cmd);
    void cmdQueryAccelerationStructureProperties(const commands::QueryAccelerationStructureProperties& cmd);
    void cmdSerializeAccelerationStructure(const commands::SerializeAccelerationStructure& cmd);
    void cmdDeserializeAccelerationStructure(const commands::DeserializeAccelerationStructure& cmd);
    void cmdExecuteClusterOperation(const commands::ExecuteClusterOperation& cmd);
    void cmdConvertCooperativeVectorMatrix(const commands::ConvertCooperativeVectorMatrix& cmd);
    void cmdSetBufferState(const commands::SetBufferState& cmd);
    void cmdSetTextureState(const commands::SetTextureState& cmd);
    void cmdGlobalBarrier(const commands::GlobalBarrier& cmd);
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

#undef NOT_IMPLEMENTED

    return SLANG_OK;
}

#define NOT_SUPPORTED(x) m_device->printWarning(x " command is not supported!")

void CommandExecutor::cmdCopyBuffer(const commands::CopyBuffer& cmd)
{
    BufferImpl* dst = checked_cast<BufferImpl*>(cmd.dst);
    BufferImpl* src = checked_cast<BufferImpl*>(cmd.src);
    std::memcpy(dst->m_data + cmd.dstOffset, src->m_data + cmd.srcOffset, cmd.size);
}

void CommandExecutor::cmdCopyTexture(const commands::CopyTexture& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_copyTexture);
}

void CommandExecutor::cmdCopyTextureToBuffer(const commands::CopyTextureToBuffer& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_copyTextureToBuffer);
}

void CommandExecutor::cmdClearBuffer(const commands::ClearBuffer& cmd)
{
    BufferImpl* buffer = checked_cast<BufferImpl*>(cmd.buffer);
    std::memset(buffer->m_data + cmd.range.offset, 0, cmd.range.size);
}

void CommandExecutor::cmdClearTextureDepthStencil(const commands::ClearTextureDepthStencil& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_clearTextureDepthStencil);
}

void CommandExecutor::cmdClearTextureFloat(const commands::ClearTextureFloat& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_clearTextureFloat);
}

void CommandExecutor::cmdClearTextureUint(const commands::ClearTextureUint& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_clearTextureUint);
}

void CommandExecutor::cmdUploadTextureData(const commands::UploadTextureData& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_uploadTextureData);
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
    NOT_SUPPORTED(S_CommandEncoder_beginRenderPass);
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
    NOT_SUPPORTED(S_RenderPassEncoder_draw);
}

void CommandExecutor::cmdDrawIndexed(const commands::DrawIndexed& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_RenderPassEncoder_drawIndexed);
}

void CommandExecutor::cmdDrawIndirect(const commands::DrawIndirect& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_RenderPassEncoder_drawIndirect);
}

void CommandExecutor::cmdDrawIndexedIndirect(const commands::DrawIndexedIndirect& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_RenderPassEncoder_drawIndexedIndirect);
}

void CommandExecutor::cmdDrawMeshTasks(const commands::DrawMeshTasks& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_RenderPassEncoder_drawMeshTasks);
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

    slang_prelude::ComputeVaryingInput varyingInput;
    varyingInput.startGroupID.x = 0;
    varyingInput.startGroupID.y = 0;
    varyingInput.startGroupID.z = 0;
    varyingInput.endGroupID.x = cmd.x;
    varyingInput.endGroupID.y = cmd.y;
    varyingInput.endGroupID.z = cmd.z;

    m_computePipeline->m_func(&varyingInput, m_bindingData->entryPoints[0].data, m_bindingData->globalData);
}

void CommandExecutor::cmdDispatchComputeIndirect(const commands::DispatchComputeIndirect& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_ComputePassEncoder_dispatchComputeIndirect);
}

void CommandExecutor::cmdBeginRayTracingPass(const commands::BeginRayTracingPass& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_beginRayTracingPass);
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
    NOT_SUPPORTED(S_RayTracingPassEncoder_dispatchRays);
}

void CommandExecutor::cmdBuildAccelerationStructure(const commands::BuildAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_buildAccelerationStructure);
}

void CommandExecutor::cmdCopyAccelerationStructure(const commands::CopyAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_copyAccelerationStructure);
}

void CommandExecutor::cmdQueryAccelerationStructureProperties(const commands::QueryAccelerationStructureProperties& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_queryAccelerationStructureProperties);
}

void CommandExecutor::cmdSerializeAccelerationStructure(const commands::SerializeAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_serializeAccelerationStructure);
}

void CommandExecutor::cmdDeserializeAccelerationStructure(const commands::DeserializeAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_deserializeAccelerationStructure);
}

void CommandExecutor::cmdExecuteClusterOperation(const commands::ExecuteClusterOperation& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_executeClusterOperation);
}

void CommandExecutor::cmdConvertCooperativeVectorMatrix(const commands::ConvertCooperativeVectorMatrix& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_convertCooperativeVectorMatrix);
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
    queryPool->m_queries[cmd.queryIndex] = std::chrono::high_resolution_clock::now().time_since_epoch().count();
}

void CommandExecutor::cmdExecuteCallback(const commands::ExecuteCallback& cmd)
{
    cmd.callback(cmd.userData);
}

// CommandQueueImpl

CommandQueueImpl::CommandQueueImpl(Device* device, QueueType type)
    : CommandQueue(device, type)
{
}

Result CommandQueueImpl::createCommandEncoder(ICommandEncoder** outEncoder)
{
    RefPtr<CommandEncoderImpl> encoder = new CommandEncoderImpl(m_device);
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

    // Execute command buffers.
    for (uint32_t i = 0; i < desc.commandBufferCount; i++)
    {
        CommandExecutor executor(getDevice<DeviceImpl>());
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

// CommandEncoderImpl

CommandEncoderImpl::CommandEncoderImpl(Device* device)
    : CommandEncoder(device)
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

Result CommandEncoderImpl::finish(ICommandBuffer** outCommandBuffer)
{
    SLANG_RETURN_ON_FAIL(resolvePipelines(m_device));
    returnComPtr(outCommandBuffer, m_commandBuffer);
    m_commandBuffer = nullptr;
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
