#include "cpu-command.h"
#include "cpu-query.h"
#include "cpu-shader-program.h"
#include "../command-list.h"
#include "../strings.h"

namespace rhi::cpu {

class CommandExecutor
{
public:
    DeviceImpl* m_device;
    ComputePipelineImpl* m_currentComputePipeline = nullptr;
    RootShaderObjectImpl* m_currentRootObject = nullptr;
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
    void cmdClearTexture(const commands::ClearTexture& cmd);
    void cmdUploadTextureData(const commands::UploadTextureData& cmd);
    void cmdUploadBufferData(const commands::UploadBufferData& cmd);
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
    void cmdSetBufferState(const commands::SetBufferState& cmd);
    void cmdSetTextureState(const commands::SetTextureState& cmd);
    void cmdBeginDebugEvent(const commands::BeginDebugEvent& cmd);
    void cmdEndDebugEvent(const commands::EndDebugEvent& cmd);
    void cmdWriteTimestamp(const commands::WriteTimestamp& cmd);
    void cmdExecuteCallback(const commands::ExecuteCallback& cmd);
};

Result CommandExecutor::execute(CommandBufferImpl* commandBuffer)
{
    CommandList* commandList = commandBuffer->m_commandList;
    auto command = commandList->getCommands();
    while (command)
    {
#define SLANG_RHI_COMMAND_EXECUTE_X(x)                                                                                 \
    case CommandID::x:                                                                                                 \
        cmd##x(commandList->getCommand<commands::x>(command));                                                         \
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

#define NOT_SUPPORTED(x) m_device->warning(S_CommandEncoder_##x " command is not supported!")

void CommandExecutor::cmdCopyBuffer(const commands::CopyBuffer& cmd)
{
    BufferImpl* dst = checked_cast<BufferImpl*>(cmd.dst);
    BufferImpl* src = checked_cast<BufferImpl*>(cmd.src);
    std::memcpy(dst->m_data + cmd.dstOffset, src->m_data + cmd.srcOffset, cmd.size);
}

void CommandExecutor::cmdCopyTexture(const commands::CopyTexture& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(copyTexture);
}

void CommandExecutor::cmdCopyTextureToBuffer(const commands::CopyTextureToBuffer& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(copyTextureToBuffer);
}

void CommandExecutor::cmdClearBuffer(const commands::ClearBuffer& cmd)
{
    BufferImpl* buffer = checked_cast<BufferImpl*>(cmd.buffer);
    std::memset(buffer->m_data + cmd.range.offset, 0, cmd.range.size);
}

void CommandExecutor::cmdClearTexture(const commands::ClearTexture& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(clearTexture);
}

void CommandExecutor::cmdUploadTextureData(const commands::UploadTextureData& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(uploadTextureData);
}

void CommandExecutor::cmdUploadBufferData(const commands::UploadBufferData& cmd)
{
    BufferImpl* dst = checked_cast<BufferImpl*>(cmd.dst);
    std::memcpy(dst->m_data + cmd.offset, cmd.data, cmd.size);
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
    NOT_SUPPORTED(beginRenderPass);
}

void CommandExecutor::cmdEndRenderPass(const commands::EndRenderPass& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(endRenderPass);
}

void CommandExecutor::cmdSetRenderState(const commands::SetRenderState& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(setRenderState);
}

void CommandExecutor::cmdDraw(const commands::Draw& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(draw);
}

void CommandExecutor::cmdDrawIndexed(const commands::DrawIndexed& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(drawIndexed);
}

void CommandExecutor::cmdDrawIndirect(const commands::DrawIndirect& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(drawIndirect);
}

void CommandExecutor::cmdDrawIndexedIndirect(const commands::DrawIndexedIndirect& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(drawIndexedIndirect);
}

void CommandExecutor::cmdDrawMeshTasks(const commands::DrawMeshTasks& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(drawMeshTasks);
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
    m_currentComputePipeline = checked_cast<ComputePipelineImpl*>(cmd.state.pipeline);
    m_currentRootObject = checked_cast<RootShaderObjectImpl*>(cmd.state.rootObject);
    m_computeStateValid = m_currentComputePipeline && m_currentRootObject;
}

void CommandExecutor::cmdDispatchCompute(const commands::DispatchCompute& cmd)
{
    if (!m_computeStateValid)
        return;

    int entryPointIndex = 0;
    int targetIndex = 0;

    auto program = m_currentComputePipeline->m_program.get();
    auto entryPointLayout = m_currentRootObject->getLayout()->getEntryPoint(entryPointIndex);
    auto entryPointName = entryPointLayout->getEntryPointName();
    auto entryPointObject = m_currentRootObject->getEntryPoint(entryPointIndex);

    ComPtr<ISlangSharedLibrary> sharedLibrary;
    ComPtr<ISlangBlob> diagnostics;
    auto compileResult =
        program->slangGlobalScope
            ->getEntryPointHostCallable(entryPointIndex, targetIndex, sharedLibrary.writeRef(), diagnostics.writeRef());
    if (diagnostics)
    {
        m_device->handleMessage(
            compileResult == SLANG_OK ? DebugMessageType::Warning : DebugMessageType::Error,
            DebugMessageSource::Slang,
            (char*)diagnostics->getBufferPointer()
        );
    }
    if (SLANG_FAILED(compileResult))
        return;

    auto func = (slang_prelude::ComputeFunc)sharedLibrary->findSymbolAddressByName(entryPointName);

    slang_prelude::ComputeVaryingInput varyingInput;
    varyingInput.startGroupID.x = 0;
    varyingInput.startGroupID.y = 0;
    varyingInput.startGroupID.z = 0;
    varyingInput.endGroupID.x = cmd.x;
    varyingInput.endGroupID.y = cmd.y;
    varyingInput.endGroupID.z = cmd.z;

    auto globalParamsData = m_currentRootObject->getDataBuffer();
    auto entryPointParamsData = entryPointObject->getDataBuffer();
    func(&varyingInput, entryPointParamsData, globalParamsData);
}

void CommandExecutor::cmdDispatchComputeIndirect(const commands::DispatchComputeIndirect& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(dispatchComputeIndirect);
}

void CommandExecutor::cmdBeginRayTracingPass(const commands::BeginRayTracingPass& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(beginRayTracingPass);
}

void CommandExecutor::cmdEndRayTracingPass(const commands::EndRayTracingPass& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(endRayTracingPass);
}

void CommandExecutor::cmdSetRayTracingState(const commands::SetRayTracingState& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(setRayTracingState);
}

void CommandExecutor::cmdDispatchRays(const commands::DispatchRays& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(dispatchRays);
}

void CommandExecutor::cmdBuildAccelerationStructure(const commands::BuildAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(buildAccelerationStructure);
}

void CommandExecutor::cmdCopyAccelerationStructure(const commands::CopyAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(copyAccelerationStructure);
}

void CommandExecutor::cmdQueryAccelerationStructureProperties(const commands::QueryAccelerationStructureProperties& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(queryAccelerationStructureProperties);
}

void CommandExecutor::cmdSerializeAccelerationStructure(const commands::SerializeAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(serializeAccelerationStructure);
}

void CommandExecutor::cmdDeserializeAccelerationStructure(const commands::DeserializeAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(deserializeAccelerationStructure);
}

void CommandExecutor::cmdSetBufferState(const commands::SetBufferState& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdSetTextureState(const commands::SetTextureState& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdBeginDebugEvent(const commands::BeginDebugEvent& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdEndDebugEvent(const commands::EndDebugEvent& cmd)
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

CommandQueueImpl::CommandQueueImpl(DeviceImpl* device, QueueType type)
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

Result CommandQueueImpl::submit(
    GfxCount count,
    ICommandBuffer* const* commandBuffers,
    IFence* fenceToSignal,
    uint64_t newFenceValue
)
{
    for (GfxIndex i = 0; i < count; i++)
    {
        CommandExecutor executor(m_device);
        SLANG_RETURN_ON_FAIL(executor.execute(checked_cast<CommandBufferImpl*>(commandBuffers[i])));
    }
    return SLANG_OK;
}

Result CommandQueueImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result CommandQueueImpl::waitOnHost()
{
    return SLANG_OK;
}

Result CommandQueueImpl::waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues)
{
    return SLANG_E_NOT_AVAILABLE;
}

// CommandEncoderImpl

CommandEncoderImpl::CommandEncoderImpl(DeviceImpl* device)
    : m_device(device)
{
}

Result CommandEncoderImpl::init()
{
    m_commandBuffer = new CommandBufferImpl();
    m_commandBuffer->m_commandList = new CommandList();
    m_commandList = m_commandBuffer->m_commandList;
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

Result CommandBufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

} // namespace rhi::cpu
