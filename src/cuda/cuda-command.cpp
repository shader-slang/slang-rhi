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

#define NOT_SUPPORTED(x) m_device->printWarning(x " command is not supported!")

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
    TextureImpl* dst = checked_cast<TextureImpl*>(cmd.dst);
    TextureImpl* src = checked_cast<TextureImpl*>(cmd.src);

    SubresourceRange dstSubresource = cmd.dstSubresource;
    const Offset3D& dstOffset = cmd.dstOffset;
    SubresourceRange srcSubresource = cmd.srcSubresource;
    const Offset3D& srcOffset = cmd.srcOffset;
    const Extent3D& extent = cmd.extent;

    // Fix up sub resource ranges if they are 0 (meaning use entire range)
    if (dstSubresource.layerCount == 0)
        dstSubresource.layerCount = dst->m_desc.getLayerCount();
    if (dstSubresource.mipCount == 0)
        dstSubresource.mipCount = dst->m_desc.mipCount;
    if (srcSubresource.layerCount == 0)
        srcSubresource.layerCount = src->m_desc.getLayerCount();
    if (srcSubresource.mipCount == 0)
        srcSubresource.mipCount = src->m_desc.mipCount;

    const FormatInfo& formatInfo = getFormatInfo(src->m_desc.format);
    Extent3D srcTextureSize = src->m_desc.size;

    // Copy each layer and mip level
    for (uint32_t layerOffset = 0; layerOffset < srcSubresource.layerCount; layerOffset++)
    {
        uint32_t srcLayer = srcSubresource.layer + layerOffset;
        uint32_t dstLayer = dstSubresource.layer + layerOffset;

        for (uint32_t mipOffset = 0; mipOffset < srcSubresource.mipCount; mipOffset++)
        {
            uint32_t srcMip = srcSubresource.mip + mipOffset;
            uint32_t dstMip = dstSubresource.mip + mipOffset;

            // Calculate adjusted extents. Note it is required and enforced
            // by debug layer that if 'remaining texture' is used, src and
            // dst offsets are the same.
            Extent3D srcMipSize = calcMipSize(srcTextureSize, srcMip);
            Extent3D adjustedExtent = extent;
            if (adjustedExtent.width == kRemainingTextureSize)
            {
                SLANG_RHI_ASSERT(srcOffset.x == dstOffset.x);
                adjustedExtent.width = srcMipSize.width - srcOffset.x;
            }
            if (adjustedExtent.height == kRemainingTextureSize)
            {
                SLANG_RHI_ASSERT(srcOffset.y == dstOffset.y);
                adjustedExtent.height = srcMipSize.height - srcOffset.y;
            }
            if (adjustedExtent.depth == kRemainingTextureSize)
            {
                SLANG_RHI_ASSERT(srcOffset.z == dstOffset.z);
                adjustedExtent.depth = srcMipSize.depth - srcOffset.z;
            }

            CUarray srcArray = src->m_cudaArray;
            CUarray dstArray = dst->m_cudaArray;

            // Get the appropriate mip level if using mipmapped arrays
            if (src->m_cudaMipMappedArray)
            {
                SLANG_CUDA_ASSERT_ON_FAIL(cuMipmappedArrayGetLevel(&srcArray, src->m_cudaMipMappedArray, srcMip));
            }
            if (dst->m_cudaMipMappedArray)
            {
                SLANG_CUDA_ASSERT_ON_FAIL(cuMipmappedArrayGetLevel(&dstArray, dst->m_cudaMipMappedArray, dstMip));
            }

            CUDA_MEMCPY3D copyParam = {};
            copyParam.srcMemoryType = CU_MEMORYTYPE_ARRAY;
            copyParam.srcArray = srcArray;
            copyParam.srcXInBytes = widthInBlocks(formatInfo, srcOffset.x) * formatInfo.blockSizeInBytes;
            copyParam.srcY = heightInBlocks(formatInfo, srcOffset.y);
            copyParam.srcZ = srcOffset.z + srcLayer;

            copyParam.dstMemoryType = CU_MEMORYTYPE_ARRAY;
            copyParam.dstArray = dstArray;
            copyParam.dstXInBytes = widthInBlocks(formatInfo, dstOffset.x) * formatInfo.blockSizeInBytes;
            copyParam.dstY = heightInBlocks(formatInfo, dstOffset.y);
            copyParam.dstZ = dstOffset.z + dstLayer;

            copyParam.WidthInBytes = widthInBlocks(formatInfo, adjustedExtent.width) * formatInfo.blockSizeInBytes;
            copyParam.Height = heightInBlocks(formatInfo, adjustedExtent.height);
            copyParam.Depth = adjustedExtent.depth;

            SLANG_CUDA_ASSERT_ON_FAIL(cuMemcpy3D(&copyParam));
        }
    }
}

void CommandExecutor::cmdCopyTextureToBuffer(const commands::CopyTextureToBuffer& cmd)
{
    BufferImpl* dst = checked_cast<BufferImpl*>(cmd.dst);
    TextureImpl* src = checked_cast<TextureImpl*>(cmd.src);

    const TextureDesc& srcDesc = src->getDesc();
    Extent3D textureSize = srcDesc.size;
    const FormatInfo& formatInfo = getFormatInfo(srcDesc.format);

    const uint64_t dstOffset = cmd.dstOffset;
    const Size dstRowPitch = cmd.dstRowPitch;
    uint32_t srcLayer = cmd.srcLayer;
    uint32_t srcMip = cmd.srcMip;
    const Offset3D& srcOffset = cmd.srcOffset;
    const Extent3D& extent = cmd.extent;

    // Calculate adjusted extents. Note it is required and enforced
    // by debug layer that if 'remaining texture' is used, src and
    // dst offsets are the same.
    Extent3D srcMipSize = calcMipSize(textureSize, srcMip);
    Extent3D adjustedExtent = extent;
    if (adjustedExtent.width == kRemainingTextureSize)
    {
        SLANG_RHI_ASSERT(srcMipSize.width >= srcOffset.x);
        adjustedExtent.width = srcMipSize.width - srcOffset.x;
    }
    if (adjustedExtent.height == kRemainingTextureSize)
    {
        SLANG_RHI_ASSERT(srcMipSize.height >= srcOffset.y);
        adjustedExtent.height = srcMipSize.height - srcOffset.y;
    }
    if (adjustedExtent.depth == kRemainingTextureSize)
    {
        SLANG_RHI_ASSERT(srcMipSize.depth >= srcOffset.z);
        adjustedExtent.depth = srcMipSize.depth - srcOffset.z;
    }

    // Align extents to block size
    adjustedExtent.width = math::calcAligned(adjustedExtent.width, formatInfo.blockWidth);
    adjustedExtent.height = math::calcAligned(adjustedExtent.height, formatInfo.blockHeight);

    // z is either base array layer or z offset depending on whether this is 3D or array texture
    SLANG_RHI_ASSERT(srcLayer == 0 || srcOffset.z == 0);
    uint32_t z = srcOffset.z + srcLayer;

    CUarray srcArray = src->m_cudaArray;

    // Get the appropriate mip level if using mipmapped arrays
    if (src->m_cudaMipMappedArray)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuMipmappedArrayGetLevel(&srcArray, src->m_cudaMipMappedArray, srcMip));
    }

    CUDA_MEMCPY3D copyParam = {};
    copyParam.srcMemoryType = CU_MEMORYTYPE_ARRAY;
    copyParam.srcArray = srcArray;
    copyParam.srcXInBytes = widthInBlocks(formatInfo, srcOffset.x) * formatInfo.blockSizeInBytes;
    copyParam.srcY = heightInBlocks(formatInfo, srcOffset.y);
    copyParam.srcZ = z;

    copyParam.dstMemoryType = CU_MEMORYTYPE_DEVICE;
    copyParam.dstDevice = (CUdeviceptr)((uint8_t*)dst->m_cudaMemory + dstOffset);
    copyParam.dstPitch = dstRowPitch;

    copyParam.WidthInBytes = widthInBlocks(formatInfo, adjustedExtent.width) * formatInfo.blockSizeInBytes;
    copyParam.Height = heightInBlocks(formatInfo, adjustedExtent.height);
    copyParam.Depth = adjustedExtent.depth;

    SLANG_CUDA_ASSERT_ON_FAIL(cuMemcpy3D(&copyParam));
}

void CommandExecutor::cmdClearBuffer(const commands::ClearBuffer& cmd)
{
    BufferImpl* buffer = checked_cast<BufferImpl*>(cmd.buffer);
    SLANG_CUDA_ASSERT_ON_FAIL(cuMemsetD32((CUdeviceptr)buffer->m_cudaMemory + cmd.range.offset, 0, cmd.range.size / 4));
}

void CommandExecutor::cmdClearTextureFloat(const commands::ClearTextureFloat& cmd)
{
    m_device->m_clearEngine
        .clearTextureFloat(m_stream, checked_cast<TextureImpl*>(cmd.texture), cmd.subresourceRange, cmd.clearValue);
}

void CommandExecutor::cmdClearTextureUint(const commands::ClearTextureUint& cmd)
{
    m_device->m_clearEngine
        .clearTextureUint(m_stream, checked_cast<TextureImpl*>(cmd.texture), cmd.subresourceRange, cmd.clearValue);
}

void CommandExecutor::cmdClearTextureDepthStencil(const commands::ClearTextureDepthStencil& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_clearTextureDepthStencil);
}

void CommandExecutor::cmdUploadTextureData(const commands::UploadTextureData& cmd)
{
    auto dst = checked_cast<TextureImpl*>(cmd.dst);
    SubresourceRange subresourceRange = cmd.subresourceRange;

    SubresourceLayout* srLayout = cmd.layouts;
    Offset bufferOffset = cmd.srcOffset;
    auto buffer = checked_cast<BufferImpl*>(cmd.srcBuffer);

    const FormatInfo& formatInfo = getFormatInfo(dst->m_desc.format);

    for (uint32_t layerOffset = 0; layerOffset < subresourceRange.layerCount; layerOffset++)
    {
        uint32_t layer = subresourceRange.layer + layerOffset;
        for (uint32_t mipOffset = 0; mipOffset < subresourceRange.mipCount; mipOffset++)
        {
            uint32_t mip = subresourceRange.mip + mipOffset;

            CUarray dstArray = dst->m_cudaArray;
            if (dst->m_cudaMipMappedArray)
            {
                SLANG_CUDA_ASSERT_ON_FAIL(cuMipmappedArrayGetLevel(&dstArray, dst->m_cudaMipMappedArray, mip));
            }

            CUDA_MEMCPY3D copyParam = {};
            copyParam.dstMemoryType = CU_MEMORYTYPE_ARRAY;
            copyParam.dstArray = dstArray;
            copyParam.dstXInBytes = widthInBlocks(formatInfo, cmd.offset.x) * formatInfo.blockSizeInBytes;
            copyParam.dstY = heightInBlocks(formatInfo, cmd.offset.y);
            copyParam.dstZ = cmd.offset.z + layer;
            copyParam.srcMemoryType = CU_MEMORYTYPE_DEVICE;
            copyParam.srcDevice = (CUdeviceptr)((uint8_t*)buffer->m_cudaMemory + bufferOffset);
            copyParam.srcPitch = srLayout->rowPitch;
            copyParam.WidthInBytes = widthInBlocks(formatInfo, srLayout->size.width) * formatInfo.blockSizeInBytes;
            copyParam.Height = heightInBlocks(formatInfo, srLayout->size.height);
            copyParam.Depth = srLayout->size.depth;
            SLANG_CUDA_ASSERT_ON_FAIL(cuMemcpy3D(&copyParam));

            bufferOffset += srLayout->sizeInBytes;
            srLayout++;
        }
    }
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
    SLANG_RHI_ASSERT(entryPointData.size >= computePipeline->m_paramBufferSize);
    void* extraOptions[] = {
        CU_LAUNCH_PARAM_BUFFER_POINTER,
        (void*)entryPointData.data,
        CU_LAUNCH_PARAM_BUFFER_SIZE,
        (void*)&computePipeline->m_paramBufferSize,
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
        computePipeline->m_sharedMemorySize,
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
    AccelerationStructureBuildDescConverter converter;
    if (converter.convert(cmd.desc, m_device->m_debugCallback) != SLANG_OK)
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
        &converter.buildOptions,
        converter.buildInputs.data(),
        converter.buildInputs.size(),
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

CommandQueueImpl::CommandQueueImpl(Device* device, QueueType type)
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
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

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
        CommandExecutor executor(getDevice<DeviceImpl>(), m_stream);
        SLANG_RETURN_ON_FAIL(executor.execute(checked_cast<CommandBufferImpl*>(desc.commandBuffers[i])));
    }

    // Signal fences.
    for (uint32_t i = 0; i < desc.signalFenceCount; ++i)
    {
        SLANG_RETURN_ON_FAIL(desc.signalFences[i]->setCurrentValue(desc.signalFenceValues[i]));
    }

    SLANG_CUDA_ASSERT_ON_FAIL(cuStreamSynchronize(m_stream));

    for (uint32_t i = 0; i < desc.commandBufferCount; i++)
        checked_cast<CommandBufferImpl*>(desc.commandBuffers[i])->reset();

    return SLANG_OK;
}

Result CommandQueueImpl::waitOnHost()
{
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuStreamSynchronize(m_stream), this);
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuCtxSynchronize(), this);
    return SLANG_OK;
}

Result CommandQueueImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::CUstream;
    outHandle->value = (uint64_t)m_stream;
    return SLANG_OK;
}

// CommandEncoderImpl

CommandEncoderImpl::CommandEncoderImpl(Device* device)
    : CommandEncoder(device)
{
}

Result CommandEncoderImpl::init()
{
    m_commandBuffer = new CommandBufferImpl(m_device);
    m_commandList = &m_commandBuffer->m_commandList;
    return SLANG_OK;
}

Result CommandEncoderImpl::getBindingData(RootShaderObject* rootObject, BindingData*& outBindingData)
{
    rootObject->trackResources(m_commandBuffer->m_trackedObjects);
    BindingDataBuilder builder;
    builder.m_device = getDevice<DeviceImpl>();
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
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

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
    m_constantBufferPool.init(checked_cast<DeviceImpl*>(device));
}

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
