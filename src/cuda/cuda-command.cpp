#include "cuda-command.h"
#include "cuda-buffer.h"
#include "cuda-query.h"
#include "cuda-shader-object-layout.h"
#include "cuda-acceleration-structure.h"
#include "cuda-shader-table.h"
#include "../command-list.h"
#include "../strings.h"

namespace rhi::cuda {

class CommandExecutor
{
public:
    DeviceImpl* m_device;
    CUstream m_stream;

    RefPtr<RootShaderObjectImpl> m_rootObject;

    bool m_computePassActive = false;
    bool m_computeStateValid = false;
    RefPtr<ComputePipelineImpl> m_computePipeline;

#if SLANG_RHI_ENABLE_OPTIX
    bool m_rayTracingPassActive = false;
    bool m_rayTracingStateValid = false;
    RefPtr<RayTracingPipelineImpl> m_rayTracingPipeline;
    RefPtr<ShaderTableImpl> m_shaderTable;
    ShaderTableImpl::Instance* m_shaderTableInstance = nullptr;
#endif

    CommandExecutor(DeviceImpl* device, CUstream stream)
        : m_device(device)
        , m_stream(stream)
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
    void cmdPushDebugGroup(const commands::PushDebugGroup& cmd);
    void cmdPopDebugGroup(const commands::PopDebugGroup& cmd);
    void cmdInsertDebugMarker(const commands::InsertDebugMarker& cmd);
    void cmdWriteTimestamp(const commands::WriteTimestamp& cmd);
    void cmdExecuteCallback(const commands::ExecuteCallback& cmd);
};

Result CommandExecutor::execute(CommandBufferImpl* commandBuffer)
{
#define NOT_IMPLEMENTED(cmd)                                                                                           \
    m_device->handleMessage(DebugMessageType::Warning, DebugMessageSource::Layer, cmd " command not implemented");

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
    SLANG_CUDA_ASSERT_ON_FAIL(cuMemcpy(
        (CUdeviceptr)((uint8_t*)dst->m_cudaMemory + cmd.dstOffset),
        (CUdeviceptr)((uint8_t*)src->m_cudaMemory + cmd.srcOffset),
        cmd.size
    ));
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
    SLANG_CUDA_ASSERT_ON_FAIL(cuMemsetD32((CUdeviceptr)buffer->m_cudaMemory + cmd.range.offset, 0, cmd.range.size));
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
    SLANG_CUDA_ASSERT_ON_FAIL(
        cuMemcpy((CUdeviceptr)((uint8_t*)dst->m_cudaMemory + cmd.offset), (CUdeviceptr)cmd.data, cmd.size)
    );
}

void CommandExecutor::cmdResolveQuery(const commands::ResolveQuery& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(resolveQuery);
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
    m_computePassActive = true;
}

void CommandExecutor::cmdEndComputePass(const commands::EndComputePass& cmd)
{
    m_computePassActive = false;
}

void CommandExecutor::cmdSetComputeState(const commands::SetComputeState& cmd)
{
    if (!m_computePassActive)
        return;

    m_computePipeline = checked_cast<ComputePipelineImpl*>(cmd.state.pipeline);
    m_rootObject = checked_cast<RootShaderObjectImpl*>(cmd.state.rootObject);
    m_computeStateValid = m_computePipeline && m_rootObject;
}

void CommandExecutor::cmdDispatchCompute(const commands::DispatchCompute& cmd)
{
    if (!m_computeStateValid)
        return;

    ComputePipelineImpl* computePipeline = m_computePipeline;
    RootShaderObjectImpl* rootObject = m_rootObject;

    // Find out thread group size from program reflection.
    auto programLayout = checked_cast<RootShaderObjectLayoutImpl*>(rootObject->getLayout());
    int kernelIndex = programLayout->getKernelIndex(computePipeline->m_kernelName);
    SLANG_RHI_ASSERT(kernelIndex != -1);
    UInt threadGroupSize[3];
    programLayout->getKernelThreadGroupSize(kernelIndex, threadGroupSize);

    // Copy global parameter data to the `SLANG_globalParams` symbol.
    {
        CUdeviceptr globalParamsSymbol = 0;
        size_t globalParamsSymbolSize = 0;
        SLANG_CUDA_ASSERT_ON_FAIL(cuModuleGetGlobal(
            &globalParamsSymbol,
            &globalParamsSymbolSize,
            computePipeline->m_module,
            "SLANG_globalParams"
        ));

        CUdeviceptr globalParamsCUDAData = (CUdeviceptr)rootObject->getBuffer();
        SLANG_CUDA_ASSERT_ON_FAIL(
            cuMemcpyAsync((CUdeviceptr)globalParamsSymbol, (CUdeviceptr)globalParamsCUDAData, globalParamsSymbolSize, 0)
        );
    }
    //
    // The argument data for the entry-point parameters are already
    // stored in host memory in a CUDAEntryPointShaderObject, as expected by cuLaunchKernel.
    //
    auto entryPointBuffer = rootObject->entryPointObjects[kernelIndex]->getBuffer();
    auto entryPointDataSize = rootObject->entryPointObjects[kernelIndex]->getBufferSize();

    void* extraOptions[] = {
        CU_LAUNCH_PARAM_BUFFER_POINTER,
        entryPointBuffer,
        CU_LAUNCH_PARAM_BUFFER_SIZE,
        &entryPointDataSize,
        CU_LAUNCH_PARAM_END,
    };

    // Once we have all the necessary data extracted and/or
    // set up, we can launch the kernel and see what happens.
    //
    SLANG_CUDA_ASSERT_ON_FAIL(cuLaunchKernel(
        computePipeline->m_function,
        cmd.x,
        cmd.y,
        cmd.z,
        int(threadGroupSize[0]),
        int(threadGroupSize[1]),
        int(threadGroupSize[2]),
        0,
        m_stream,
        nullptr,
        extraOptions
    ));
}

void CommandExecutor::cmdDispatchComputeIndirect(const commands::DispatchComputeIndirect& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(dispatchComputeIndirect);
}

void CommandExecutor::cmdBeginRayTracingPass(const commands::BeginRayTracingPass& cmd)
{
    SLANG_UNUSED(cmd);
#if SLANG_RHI_ENABLE_OPTIX
    m_rayTracingPassActive = true;
#else
    NOT_SUPPORTED(beginRayTracingPass);
#endif
}

void CommandExecutor::cmdEndRayTracingPass(const commands::EndRayTracingPass& cmd)
{
    SLANG_UNUSED(cmd);
#if SLANG_RHI_ENABLE_OPTIX
    m_rayTracingPassActive = false;
#else
    NOT_SUPPORTED(endRayTracingPass);
#endif
}

void CommandExecutor::cmdSetRayTracingState(const commands::SetRayTracingState& cmd)
{
#if SLANG_RHI_ENABLE_OPTIX
    if (!m_rayTracingPassActive)
        return;

    m_rayTracingPipeline = checked_cast<RayTracingPipelineImpl*>(cmd.state.pipeline);
    m_rootObject = checked_cast<RootShaderObjectImpl*>(cmd.state.rootObject);
    m_shaderTable = checked_cast<ShaderTableImpl*>(cmd.state.shaderTable);
    m_shaderTableInstance = m_shaderTable ? m_shaderTable->getInstance(m_rayTracingPipeline) : nullptr;
    m_rayTracingStateValid = m_rayTracingPipeline && m_rootObject && m_shaderTable;
#else
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(setRayTracingState);
#endif
}

void CommandExecutor::cmdDispatchRays(const commands::DispatchRays& cmd)
{
#if SLANG_RHI_ENABLE_OPTIX
    if (!m_rayTracingStateValid)
        return;

    CUdeviceptr paramsData = (CUdeviceptr)m_rootObject->getBuffer();
    size_t paramsSize = m_rootObject->getBufferSize();

    // TODO: slang returns 16 bytes for OptixTraversableHandle which is incorrect!
    paramsSize = 0;
    int fieldCount = m_rootObject->getLayout()->getElementTypeLayout()->getFieldCount();
    for (int fieldIndex = 0; fieldIndex < fieldCount; ++fieldIndex)
    {
        auto field = m_rootObject->getLayout()->getElementTypeLayout()->getFieldByIndex(fieldIndex);
        bool isOptixTraversableHandle =
            std::strcmp(field->getType()->getName(), "RaytracingAccelerationStructure") == 0;
        paramsSize += isOptixTraversableHandle ? 8 : field->getTypeLayout()->getSize();
    }

    OptixShaderBindingTable sbt = m_shaderTableInstance->sbt;
    sbt.raygenRecord += cmd.rayGenShaderIndex * m_shaderTableInstance->raygenRecordSize;

    SLANG_OPTIX_ASSERT_ON_FAIL(optixLaunch(
        m_rayTracingPipeline->m_pipeline,
        m_stream,
        paramsData,
        paramsSize,
        &sbt,
        cmd.width,
        cmd.height,
        cmd.depth
    ));
#else
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(dispatchRays);
#endif
}

void CommandExecutor::cmdBuildAccelerationStructure(const commands::BuildAccelerationStructure& cmd)
{
#if SLANG_RHI_ENABLE_OPTIX
    AccelerationStructureBuildInputBuilder builder;
    if (builder.build(cmd.desc, m_device->m_debugCallback) != SLANG_OK)
        return;

    AccelerationStructureImpl* dst = checked_cast<AccelerationStructureImpl*>(cmd.dst);

    short_vector<OptixAccelEmitDesc, 8> emittedProperties;
    for (GfxCount i = 0; i < cmd.propertyQueryCount; i++)
    {
        if (cmd.queryDescs[i].queryType == QueryType::AccelerationStructureCompactedSize)
        {
            PlainBufferProxyQueryPoolImpl* queryPool =
                checked_cast<PlainBufferProxyQueryPoolImpl*>(cmd.queryDescs[i].queryPool);
            OptixAccelEmitDesc property = {};
            property.type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
            property.result = queryPool->m_buffer + cmd.queryDescs[i].firstQueryIndex * sizeof(uint64_t);
            emittedProperties.push_back(property);
        }
    }

    SLANG_OPTIX_ASSERT_ON_FAIL(optixAccelBuild(
        m_device->m_ctx.optixContext,
        m_stream,
        &builder.buildOptions,
        builder.buildInputs.data(),
        builder.buildInputs.size(),
        cmd.scratchBuffer.getDeviceAddress(),
        checked_cast<BufferImpl*>(cmd.scratchBuffer.buffer)->m_desc.size - cmd.scratchBuffer.offset,
        dst->m_buffer,
        dst->m_desc.size,
        &dst->m_handle,
        emittedProperties.empty() ? nullptr : emittedProperties.data(),
        emittedProperties.size()
    ));
#else  // SLANG_RHI_ENABLE_OPTIX
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(buildAccelerationStructure);
#endif // SLANG_RHI_ENABLE_OPTIX
}

void CommandExecutor::cmdCopyAccelerationStructure(const commands::CopyAccelerationStructure& cmd)
{
#if SLANG_RHI_ENABLE_OPTIX
    AccelerationStructureImpl* dst = checked_cast<AccelerationStructureImpl*>(cmd.dst);
    AccelerationStructureImpl* src = checked_cast<AccelerationStructureImpl*>(cmd.src);

    switch (cmd.mode)
    {
    case AccelerationStructureCopyMode::Clone:
    {
#if 0
                OptixRelocationInfo relocInfo = {};
                optixAccelGetRelocationInfo(m_commandBuffer->m_device->m_ctx.optixContext, src->m_handle, &relocInfo);

                // TODO setup inputs
                OptixRelocateInput relocInput = {};

                cuMemcpyDtoD(dst->m_buffer, src->m_buffer, src->m_desc.size);

                optixAccelRelocate(
                    m_commandBuffer->m_device->m_ctx.optixContext,
                    m_stream,
                    &relocInfo,
                    &relocInput,
                    1,
                    dst->m_buffer,
                    dst->m_desc.size,
                    &dst->m_handle
                );
                break;
#endif
    }
    case AccelerationStructureCopyMode::Compact:
        SLANG_OPTIX_ASSERT_ON_FAIL(optixAccelCompact(
            m_device->m_ctx.optixContext,
            m_stream,
            src->m_handle,
            dst->m_buffer,
            dst->m_desc.size,
            &dst->m_handle
        ));
        break;
    }
#else  // SLANG_RHI_ENABLE_OPTIX
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(copyAccelerationStructure);
#endif // SLANG_RHI_ENABLE_OPTIX
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
    SLANG_CUDA_ASSERT_ON_FAIL(cuEventRecord(queryPool->m_events[cmd.queryIndex], m_stream));
}

void CommandExecutor::cmdExecuteCallback(const commands::ExecuteCallback& cmd)
{
    cmd.callback(cmd.userData);
}

// CommandQueueImpl

CommandQueueImpl::CommandQueueImpl(DeviceImpl* device, QueueType type)
    : CommandQueue(device, type)
{
    SLANG_CUDA_ASSERT_ON_FAIL(cuStreamCreate(&m_stream, 0));
}

CommandQueueImpl::~CommandQueueImpl()
{
    SLANG_CUDA_ASSERT_ON_FAIL(cuStreamSynchronize(m_stream));
    SLANG_CUDA_ASSERT_ON_FAIL(cuStreamDestroy(m_stream));
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
    IFence* fence,
    uint64_t valueToSignal
)
{
    SLANG_UNUSED(valueToSignal);
    // TODO: implement fence.
    SLANG_RHI_ASSERT(fence == nullptr);
    for (GfxIndex i = 0; i < count; i++)
    {
        CommandExecutor executor(m_device, m_stream);
        SLANG_RETURN_ON_FAIL(executor.execute(checked_cast<CommandBufferImpl*>(commandBuffers[i])));
    }
    SLANG_CUDA_ASSERT_ON_FAIL(cuStreamSynchronize(m_stream));
    return SLANG_OK;
}

Result CommandQueueImpl::waitOnHost()
{
    SLANG_CUDA_RETURN_ON_FAIL(cuStreamSynchronize(m_stream));
    SLANG_CUDA_RETURN_ON_FAIL(cuCtxSynchronize());
    return SLANG_OK;
}

Result CommandQueueImpl::waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues)
{
    return SLANG_FAIL;
}

Result CommandQueueImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::CUstream;
    outHandle->value = (uint64_t)m_stream;
    return SLANG_OK;
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

} // namespace rhi::cuda
