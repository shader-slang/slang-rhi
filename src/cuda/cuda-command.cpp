#include "cuda-command.h"
#include "cuda-device.h"
#include "cuda-pipeline.h"
#include "cuda-buffer.h"
#include "cuda-query.h"
#include "cuda-shader-object-layout.h"
#include "cuda-acceleration-structure.h"
#include "cuda-shader-table.h"
#include "cuda-shader-object.h"
#include "../command-list.h"
#include "../strings.h"

namespace rhi::cuda {

class CommandExecutor
{
public:
    DeviceImpl* m_device;
    CUstream m_stream;

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

    BindingDataImpl* m_bindingData = nullptr;

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
    void cmdConvertCooperativeVectorMatrix(const commands::ConvertCooperativeVectorMatrix& cmd);
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

    // Upload constant buffer data
    commandBuffer->m_constantBufferPool.upload(m_stream);

    const CommandList& commandList = commandBuffer->m_commandList;
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

#define NOT_SUPPORTED(x) m_device->warning(x " command is not supported!")

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
    SLANG_CUDA_ASSERT_ON_FAIL(cuMemsetD32((CUdeviceptr)buffer->m_cudaMemory + cmd.range.offset, 0, cmd.range.size));
}

void CommandExecutor::cmdClearTexture(const commands::ClearTexture& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_clearTexture);
}

void CommandExecutor::cmdUploadTextureData(const commands::UploadTextureData& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_uploadTextureData);
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
    NOT_SUPPORTED(S_CommandEncoder_resolveQuery);
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

    m_computePipeline = checked_cast<ComputePipelineImpl*>(cmd.pipeline);
    m_bindingData = static_cast<BindingDataImpl*>(cmd.bindingData);
    m_computeStateValid = m_computePipeline && m_bindingData;
}

void CommandExecutor::cmdDispatchCompute(const commands::DispatchCompute& cmd)
{
    if (!m_computeStateValid)
        return;

    ComputePipelineImpl* computePipeline = m_computePipeline;
    BindingDataImpl* bindingData = m_bindingData;

    SLANG_RHI_ASSERT(computePipeline->m_kernelIndex < bindingData->entryPointCount);
    const auto& entryPointData = bindingData->entryPoints[computePipeline->m_kernelIndex];

    // Copy global parameter data to the `SLANG_globalParams` symbol.
    {
        CUdeviceptr globalParamsSymbol = 0;
        size_t globalParamsSymbolSize = 0;
        CUresult result = cuModuleGetGlobal(
            &globalParamsSymbol,
            &globalParamsSymbolSize,
            computePipeline->m_module,
            "SLANG_globalParams"
        );
        if (result == CUDA_SUCCESS)
        {
            SLANG_RHI_ASSERT(globalParamsSymbolSize == bindingData->globalParamsSize);
            SLANG_CUDA_ASSERT_ON_FAIL(
                cuMemcpyAsync(globalParamsSymbol, bindingData->globalParams, globalParamsSymbolSize, m_stream)
            );
        }
    }
    //
    // The argument data for the entry-point parameters are already
    // stored in host memory, as expected by cuLaunchKernel.
    //
    void* extraOptions[] = {
        CU_LAUNCH_PARAM_BUFFER_POINTER,
        (void*)entryPointData.data,
        CU_LAUNCH_PARAM_BUFFER_SIZE,
        (void*)&entryPointData.size,
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
        computePipeline->m_threadGroupSize[0],
        computePipeline->m_threadGroupSize[1],
        computePipeline->m_threadGroupSize[2],
        0,
        m_stream,
        nullptr,
        extraOptions
    ));
}

void CommandExecutor::cmdDispatchComputeIndirect(const commands::DispatchComputeIndirect& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_ComputePassEncoder_dispatchComputeIndirect);
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

    m_rayTracingPipeline = checked_cast<RayTracingPipelineImpl*>(cmd.pipeline);
    m_bindingData = static_cast<BindingDataImpl*>(cmd.bindingData);
    m_shaderTable = checked_cast<ShaderTableImpl*>(cmd.shaderTable);
    m_shaderTableInstance = m_shaderTable ? m_shaderTable->getInstance(m_rayTracingPipeline) : nullptr;
    m_rayTracingStateValid = m_rayTracingPipeline && m_bindingData && m_shaderTable;
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

    BindingDataImpl* bindingData = m_bindingData;

    OptixShaderBindingTable sbt = m_shaderTableInstance->sbt;
    sbt.raygenRecord += cmd.rayGenShaderIndex * m_shaderTableInstance->raygenRecordSize;

    SLANG_OPTIX_ASSERT_ON_FAIL(optixLaunch(
        m_rayTracingPipeline->m_pipeline,
        m_stream,
        bindingData->globalParams,
        bindingData->globalParamsSize,
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
    for (uint32_t i = 0; i < cmd.propertyQueryCount; i++)
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
        CommandExecutor executor(m_device, m_stream);
        SLANG_RETURN_ON_FAIL(executor.execute(checked_cast<CommandBufferImpl*>(desc.commandBuffers[i])));
    }

    // Signal fences.
    for (uint32_t i = 0; i < desc.signalFenceCount; ++i)
    {
        SLANG_RETURN_ON_FAIL(desc.signalFences[i]->setCurrentValue(desc.signalFenceValues[i]));
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
    m_commandList = &m_commandBuffer->m_commandList;
    return SLANG_OK;
}

Device* CommandEncoderImpl::getDevice()
{
    return m_device;
}

Result CommandEncoderImpl::getBindingData(RootShaderObject* rootObject, BindingData*& outBindingData)
{
    rootObject->trackResources(m_commandBuffer->m_trackedObjects);
    BindingDataBuilder builder;
    builder.m_device = m_device;
    builder.m_bindingCache = &m_commandBuffer->m_bindingCache;
    builder.m_allocator = &m_commandBuffer->m_allocator;
    builder.m_constantBufferPool = &m_commandBuffer->m_constantBufferPool;
    ShaderObjectLayout* specializedLayout = nullptr;
    SLANG_RETURN_ON_FAIL(rootObject->getSpecializedLayout(specializedLayout));
    return builder.bindAsRoot(
        rootObject,
        checked_cast<RootShaderObjectLayoutImpl*>(specializedLayout),
        (BindingDataImpl*&)outBindingData
    );
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

Result CommandBufferImpl::reset()
{
    m_bindingCache.reset();
    m_constantBufferPool.reset();
    return CommandBuffer::reset();
}

Result CommandBufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

} // namespace rhi::cuda
