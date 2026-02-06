#include "cuda-command.h"
#include "cuda-device.h"
#include "cuda-heap.h"
#include "cuda-pipeline.h"
#include "cuda-buffer.h"
#include "cuda-query.h"
#include "cuda-shader-object-layout.h"
#include "cuda-acceleration-structure.h"
#include "cuda-shader-table.h"
#include "cuda-shader-object.h"
#include "cuda-utils.h"
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

    bool m_rayTracingPassActive = false;
    bool m_rayTracingStateValid = false;
    RefPtr<RayTracingPipelineImpl> m_rayTracingPipeline;
    RefPtr<ShaderTableImpl> m_shaderTable;
    optix::ShaderBindingTable* m_shaderBindingTable = nullptr;

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

    SLANG_RHI_ASSERT(computePipeline->m_entryPointIndex < bindingData->entryPointCount);
    const auto& entryPointData = bindingData->entryPoints[computePipeline->m_entryPointIndex];

    // Copy global parameter data to the `SLANG_globalParams` symbol.
    if (computePipeline->m_globalParams)
    {
        // TODO: Slang sometimes computes the size of the global parameters layout incorrectly.
        // Instead of the assert, we currently warn about this mismatch once.
        // SLANG_RHI_ASSERT(computePipeline->m_globalParamsSize == bindingData->globalParamsSize);
        if (computePipeline->m_globalParamsSize != bindingData->globalParamsSize &&
            !computePipeline->m_warnedAboutGlobalParamsSizeMismatch)
        {
            m_device->printWarning(
                "Warning: Incorrect global parameter size (expected %llu, got %llu) for pipeline %s",
                computePipeline->m_globalParamsSize,
                bindingData->globalParamsSize,
                computePipeline->m_entryPointName.c_str()
            );
            computePipeline->m_warnedAboutGlobalParamsSizeMismatch = true;
        }
        SLANG_CUDA_ASSERT_ON_FAIL(cuMemcpyAsync(
            computePipeline->m_globalParams,
            bindingData->globalParams,
            min(bindingData->globalParamsSize, computePipeline->m_globalParamsSize),
            m_stream
        ));
    }

    // The argument data for the entry-point parameters are already
    // stored in host memory, as expected by cuLaunchKernel.
    void* extraOptions[] = {
        CU_LAUNCH_PARAM_BUFFER_POINTER,
        (void*)entryPointData.data,
        CU_LAUNCH_PARAM_BUFFER_SIZE,
        (void*)&entryPointData.size,
        CU_LAUNCH_PARAM_END,
    };

    // Once we have all the necessary data extracted and/or set up, we can launch the kernel.
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
    if (!m_computeStateValid)
        return;

    // Emulate indirect dispatch by reading the dispatch arguments from the buffer on the host.
    // This requires a GPU->CPU synchronization and stalls the GPU pipeline, but CUDA does not provide
    // a way to perform indirect dispatches natively.
    // In the future, we might consider using CUDA dynamic parallelism to launch a helper kernel
    // that performs the actual dispatch, but that would require more setup and complexity.
    BufferImpl* argBuffer = checked_cast<BufferImpl*>(cmd.argBuffer.buffer);
    CUdeviceptr srcPtr = (CUdeviceptr)argBuffer->m_cudaMemory + cmd.argBuffer.offset;

    IndirectDispatchArguments args;
    SLANG_CUDA_ASSERT_ON_FAIL(cuMemcpyDtoH(&args, srcPtr, sizeof(IndirectDispatchArguments)));

    // Skip dispatch if any dimension is zero (CUDA doesn't allow zero-sized dispatches).
    if (args.threadGroupCountX == 0 || args.threadGroupCountY == 0 || args.threadGroupCountZ == 0)
        return;

    // Dispatch with the retrieved arguments.
    commands::DispatchCompute dispatchCmd;
    dispatchCmd.x = args.threadGroupCountX;
    dispatchCmd.y = args.threadGroupCountY;
    dispatchCmd.z = args.threadGroupCountZ;
    cmdDispatchCompute(dispatchCmd);
}

void CommandExecutor::cmdBeginRayTracingPass(const commands::BeginRayTracingPass& cmd)
{
    SLANG_UNUSED(cmd);
    m_rayTracingPassActive = true;
}

void CommandExecutor::cmdEndRayTracingPass(const commands::EndRayTracingPass& cmd)
{
    SLANG_UNUSED(cmd);
    m_rayTracingPassActive = false;
}

void CommandExecutor::cmdSetRayTracingState(const commands::SetRayTracingState& cmd)
{
    if (!m_rayTracingPassActive)
        return;

    m_rayTracingPipeline = checked_cast<RayTracingPipelineImpl*>(cmd.pipeline);
    m_bindingData = static_cast<BindingDataImpl*>(cmd.bindingData);
    m_shaderTable = checked_cast<ShaderTableImpl*>(cmd.shaderTable);
    m_shaderBindingTable = m_shaderTable ? m_shaderTable->getShaderBindingTable(m_rayTracingPipeline) : nullptr;
    m_rayTracingStateValid = m_rayTracingPipeline && m_bindingData && m_shaderTable;
}

void CommandExecutor::cmdDispatchRays(const commands::DispatchRays& cmd)
{
    if (!m_rayTracingStateValid)
        return;

    if (!m_device->m_ctx.optixContext)
        return;

    BindingDataImpl* bindingData = m_bindingData;
    m_device->m_ctx.optixContext->dispatchRays(
        m_stream,
        m_rayTracingPipeline->m_optixPipeline,
        bindingData,
        m_shaderBindingTable,
        cmd.rayGenShaderIndex,
        cmd.width,
        cmd.height,
        cmd.depth
    );
}

void CommandExecutor::cmdBuildAccelerationStructure(const commands::BuildAccelerationStructure& cmd)
{
    if (!m_device->m_ctx.optixContext)
        return;

    m_device->m_ctx.optixContext->buildAccelerationStructure(
        m_stream,
        cmd.desc,
        checked_cast<AccelerationStructureImpl*>(cmd.dst),
        checked_cast<AccelerationStructureImpl*>(cmd.src),
        cmd.scratchBuffer,
        cmd.propertyQueryCount,
        cmd.queryDescs
    );
}

void CommandExecutor::cmdCopyAccelerationStructure(const commands::CopyAccelerationStructure& cmd)
{
    if (!m_device->m_ctx.optixContext)
        return;

    m_device->m_ctx.optixContext->copyAccelerationStructure(
        m_stream,
        checked_cast<AccelerationStructureImpl*>(cmd.dst),
        checked_cast<AccelerationStructureImpl*>(cmd.src),
        cmd.mode
    );
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
    if (!m_device->m_ctx.optixContext)
        return;

    m_device->m_ctx.optixContext->executeClusterOperation(m_stream, cmd.desc);
}

void CommandExecutor::cmdConvertCooperativeVectorMatrix(const commands::ConvertCooperativeVectorMatrix& cmd)
{
    if (!m_device->m_ctx.optixContext)
        return;

    m_device->m_ctx.optixContext->convertCooperativeVectorMatrix(
        m_stream,
        cmd.dstBuffer->getDeviceAddress(),
        cmd.dstDescs,
        cmd.srcBuffer->getDeviceAddress(),
        cmd.srcDescs,
        cmd.matrixCount
    );
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
}

CommandQueueImpl::~CommandQueueImpl() {}

Result CommandQueueImpl::init()
{
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

    // On CUDA, treat the graphics stream as the default stream, identified
    // by a NULL ptr. When we support async compute queues on D3D/Vulkan,
    // they will be equivalent to secondary, non-default streams in CUDA.
    if (m_type == QueueType::Graphics)
    {
        m_stream = nullptr;
    }
    else
    {
        SLANG_RETURN_ON_FAIL(cuStreamCreate(&m_stream, 0));
    }

    return SLANG_OK;
}

void CommandQueueImpl::shutdown()
{
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

    // Block on and destroy all events (multi-stream case)
    // We must destroy events explicitly - cuEventDestroy does NOT happen automatically
    // when the context is destroyed, and leaking handles can exhaust per-process limits.
    for (const auto& ev : m_submitEvents)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuEventSynchronize(ev.event));
        SLANG_CUDA_ASSERT_ON_FAIL(cuEventDestroy(ev.event));
    }
    m_submitEvents.clear();

    // Always synchronize the CUDA context before destruction.
    // This is critical for single-stream workloads using the default stream (m_stream == nullptr)
    // where lazy events optimization means m_submitEvents is empty.
    // Without this, heap destruction could call cuMemFree while GPU is still using memory!
    SLANG_CUDA_ASSERT_ON_FAIL(cuCtxSynchronize());

    // Retire finished command buffers, which should be all of them now
    retireCommandBuffers();
    SLANG_RHI_ASSERT(m_commandBuffersInFlight.empty());

    // Release all command buffers in order to release all resources they may hold.
    m_commandBuffersPool.clear();
    // Execute remaining deferred deletes.
    m_lastFinishedID = m_lastSubmittedID;
    executeDeferredDeletes();
    SLANG_RHI_ASSERT(m_deferredDeleteQueue.empty());

    // Destroy non-default streams (default stream uses nullptr, nothing to destroy)
    if (m_stream)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuStreamDestroy(m_stream));
    }
}

Result CommandQueueImpl::createCommandBuffer(CommandBufferImpl** outCommandBuffer)
{
    RefPtr<CommandBufferImpl> commandBuffer = new CommandBufferImpl(m_device);
    returnRefPtr(outCommandBuffer, commandBuffer);
    return SLANG_OK;
}

Result CommandQueueImpl::getOrCreateCommandBuffer(CommandBufferImpl** outCommandBuffer)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    RefPtr<CommandBufferImpl> commandBuffer;
    if (m_commandBuffersPool.empty())
    {
        // Pool exhausted - try lazy retirement to reclaim completed buffers
        // Use locked version since we already hold m_mutex
        retireCommandBuffersLocked();

        // If retirement freed up buffers, use them instead of allocating new
        if (!m_commandBuffersPool.empty())
        {
            commandBuffer = m_commandBuffersPool.front();
            m_commandBuffersPool.pop_front();
            commandBuffer->setInternalReferenceCount(0);
        }
        else
        {
            // Still empty after retirement - must create new
            SLANG_RETURN_ON_FAIL(createCommandBuffer(commandBuffer.writeRef()));
        }
    }
    else
    {
        commandBuffer = m_commandBuffersPool.front();
        m_commandBuffersPool.pop_front();
        commandBuffer->setInternalReferenceCount(0);
    }
    returnRefPtr(outCommandBuffer, commandBuffer);
    return SLANG_OK;
}

void CommandQueueImpl::retireCommandBuffer(CommandBufferImpl* commandBuffer)
{
    commandBuffer->reset();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_commandBuffersPool.push_back(commandBuffer);
        commandBuffer->setInternalReferenceCount(1);
    }
}

void CommandQueueImpl::retireCommandBufferLocked(CommandBufferImpl* commandBuffer)
{
    // NOTE: Caller must hold m_mutex!
    commandBuffer->reset();
    m_commandBuffersPool.push_back(commandBuffer);
    commandBuffer->setInternalReferenceCount(1);
}

Result CommandQueueImpl::retireCommandBuffers()
{
    // Public API - acquires lock, then calls locked version
    std::lock_guard<std::mutex> lock(m_mutex);
    return retireCommandBuffersLocked();
}

Result CommandQueueImpl::retireCommandBuffersLocked()
{
    // NOTE: Caller must hold m_mutex!
    //
    // Use event-based tracking if events exist, otherwise use cuStreamQuery (non-blocking).
    // Single-stream workloads skip event creation entirely for better performance.
    if (!m_submitEvents.empty())
    {
        // Multi-stream or explicit fences: use event-based tracking
        SLANG_RETURN_ON_FAIL(updateFence());
    }
    else if (!m_commandBuffersInFlight.empty())
    {
        // Single-stream lazy mode: use cuStreamQuery (non-blocking)
        CUresult result = cuStreamQuery(m_stream);
        if (result == CUDA_SUCCESS)
        {
            // Stream is idle - all submitted work is complete
            m_lastFinishedID = m_lastSubmittedID;
        }
        else if (result != CUDA_ERROR_NOT_READY)
        {
            // Unexpected error - assert in debug builds to catch bugs
            // Don't fail in release to avoid deadlocks during cleanup
            SLANG_RHI_ASSERT_FAILURE("Unexpected cuStreamQuery error");
        }
        // CUDA_ERROR_NOT_READY means work is still pending - that's fine, we're async
    }

    // Retire command buffers that have passed the submission ID
    auto cbIt = m_commandBuffersInFlight.begin();
    while (cbIt != m_commandBuffersInFlight.end())
    {
        RefPtr<CommandBufferImpl>& commandBuffer = *cbIt;
        if (commandBuffer->m_submissionID > m_lastFinishedID)
            break;

        retireCommandBufferLocked(commandBuffer);
        cbIt = m_commandBuffersInFlight.erase(cbIt);
    }

    // Delete deferred resources that are no longer in use by the GPU.
    executeDeferredDeletes();

    // Flush device heaps to process any pending frees
    // Note: With same-stream immediate reuse, most allocations are retired immediately
    // and never enter the pending list. This flush now only processes the rare
    // cross-stream allocations that were deferred. Much cheaper than before!
    SLANG_RETURN_ON_FAIL(getDevice<DeviceImpl>()->flushHeaps());

    return SLANG_OK;
}

void CommandQueueImpl::deferDelete(Resource* resource)
{
    std::lock_guard<std::mutex> lock(m_deferredDeleteQueueMutex);
    m_deferredDeleteQueue.push({m_lastSubmittedID, resource});
}

void CommandQueueImpl::executeDeferredDeletes()
{
    uint64_t lastFinishedID = m_lastFinishedID;
    std::lock_guard<std::mutex> lock(m_deferredDeleteQueueMutex);
    while (!m_deferredDeleteQueue.empty() && m_deferredDeleteQueue.front().submissionID <= lastFinishedID)
    {
        delete m_deferredDeleteQueue.front().resource;
        m_deferredDeleteQueue.pop();
    }
}

Result CommandQueueImpl::createCommandEncoder(ICommandEncoder** outEncoder)
{
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

    RefPtr<CommandEncoderImpl> encoder = new CommandEncoderImpl(m_device, this);
    SLANG_RETURN_ON_FAIL(encoder->init());
    returnComPtr(outEncoder, encoder);
    return SLANG_OK;
}

Result CommandQueueImpl::signalFence(CUstream stream, uint64_t* outId)
{
    // Increment submit count
    m_lastSubmittedID++;

    // Record submission event so we can detect completion
    SubmitEvent ev;
    ev.submitID = m_lastSubmittedID;
    SLANG_CUDA_RETURN_ON_FAIL(cuEventCreate(&ev.event, CU_EVENT_DISABLE_TIMING));
    SLANG_CUDA_RETURN_ON_FAIL(cuEventRecord(ev.event, stream));
    m_submitEvents.push_back(ev);

    if (outId)
        *outId = m_lastSubmittedID;
    return SLANG_OK;
}

Result CommandQueueImpl::updateFence()
{
    // Iterate the submit events to update the last finished ID
    auto submitIt = m_submitEvents.begin();
    while (submitIt != m_submitEvents.end())
    {
        CUresult result = cuEventQuery(submitIt->event);
        if (result == CUDA_SUCCESS)
        {
            // Event is complete.
            // We aren't recycling, so all we have to do is destroy the event
            SLANG_CUDA_ASSERT_ON_FAIL(cuEventDestroy(submitIt->event));
            m_lastFinishedID = submitIt->submitID;

            // Remove the event from the list.
            submitIt = m_submitEvents.erase(submitIt);
        }
        else if (result == CUDA_ERROR_NOT_READY)
        {
            // Not ready means event hasn't been triggered yet, so it's still in-flight.
            // As command buffers are ordered, this should mean that all subsequent events
            // are also still in-flight, so we can stop checking.
            break;
        }
        else
        {
            SLANG_CUDA_RETURN_ON_FAIL_REPORT(result, this);
        }
    }
    return SLANG_OK;
}


Result CommandQueueImpl::submit(const SubmitDesc& desc)
{
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

    // Lazy retirement: don't retire on every submit - only on pool exhaustion,
    // explicit sync, or memory pressure. Eliminates per-submit overhead.

    // Select either the queue's default stream or the stream
    // specified in the descriptor,and switch to it for the scope
    // of this submission.
    CUstream requestedStream = desc.cudaStream == kInvalidCUDAStream ? m_stream : (CUstream)desc.cudaStream;

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
        // Get/execute the buffer.
        CommandBufferImpl* commandBuffer = checked_cast<CommandBufferImpl*>(desc.commandBuffers[i]);
        CommandExecutor executor(getDevice<DeviceImpl>(), requestedStream);
        SLANG_RETURN_ON_FAIL(executor.execute(commandBuffer));

        // Lazy events: only create events for multi-stream workloads.
        // Single-stream uses cuStreamQuery() instead - zero event overhead.
        bool needsEvent = (requestedStream != m_stream);

        if (needsEvent)
        {
            // Multi-stream: use event-based tracking for cross-stream synchronization
            uint64_t submissionID;
            SLANG_RETURN_ON_FAIL(signalFence(requestedStream, &submissionID));
            commandBuffer->m_submissionID = submissionID;
        }
        else
        {
            // Single-stream lazy mode: just increment ID, no event
            m_lastSubmittedID++;
            commandBuffer->m_submissionID = m_lastSubmittedID;
        }

        m_commandBuffersInFlight.push_back(commandBuffer);
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
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuStreamSynchronize(m_stream), this);
    SLANG_CUDA_RETURN_ON_FAIL_REPORT(cuCtxSynchronize(), this);

    // Retire command buffers that have completed.
    retireCommandBuffers();

    // If there's any left, it represents a bug in slang-rhi
    SLANG_RHI_ASSERT(m_commandBuffersInFlight.empty());

    return SLANG_OK;
}

Result CommandQueueImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::CUstream;
    outHandle->value = (uint64_t)m_stream;
    return SLANG_OK;
}

// CommandEncoderImpl

CommandEncoderImpl::CommandEncoderImpl(Device* device, CommandQueueImpl* queue)
    : CommandEncoder(device)
    , m_queue(queue)
{
}

CommandEncoderImpl::~CommandEncoderImpl()
{
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

    // If the command buffer was not used, return it to the pool.
    if (m_commandBuffer)
    {
        m_queue->retireCommandBuffer(m_commandBuffer);
    }
}

Result CommandEncoderImpl::init()
{
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

    SLANG_RETURN_ON_FAIL(m_queue->getOrCreateCommandBuffer(m_commandBuffer.writeRef()));
    m_commandList = &m_commandBuffer->m_commandList;
    return SLANG_OK;
}

/// Track resources for CUDA backend, skipping device-local buffers.
/// Device-local buffers rely on CUDA stream FIFO ordering for safe reuse.
/// We still track textures, upload/readback buffers, and other resources.
static void trackResourcesForCUDA(ShaderObject* shaderObject, std::set<RefPtr<RefObject>>& resources)
{
    // Track slot resources, but skip device-local buffers
    for (const auto& slot : shaderObject->m_slots)
    {
        if (slot.resource)
        {
            // Check if this is a device-local buffer we can skip
            if (Buffer* buffer = dynamic_cast<Buffer*>(slot.resource.get()))
            {
                // Only skip DeviceLocal buffers - these benefit from same-stream reuse
                // Keep tracking Upload/ReadBack buffers as CPU may access them
                if (buffer->m_desc.memoryType == MemoryType::DeviceLocal)
                {
                    continue; // Skip tracking - CUDA stream ordering provides safety
                }
            }
            resources.insert(slot.resource);
        }
        if (slot.resource2)
        {
            // resource2 is typically a sampler or counter buffer, always track
            resources.insert(slot.resource2);
        }
    }

    // Recursively track sub-objects
    for (const auto& object : shaderObject->m_objects)
    {
        if (object)
        {
            trackResourcesForCUDA(object, resources);
        }
    }
}

static void trackResourcesForCUDARoot(RootShaderObject* rootObject, std::set<RefPtr<RefObject>>& resources)
{
    trackResourcesForCUDA(rootObject, resources);
    for (const auto& entryPoint : rootObject->m_entryPoints)
    {
        if (entryPoint)
        {
            trackResourcesForCUDA(entryPoint, resources);
        }
    }
}

Result CommandEncoderImpl::getBindingData(RootShaderObject* rootObject, BindingData*& outBindingData)
{
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());

    // Skip tracking device-local buffers - CUDA stream ordering guarantees safety
    trackResourcesForCUDARoot(rootObject, m_commandBuffer->m_trackedObjects);

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
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());
    m_constantBufferPool.init(checked_cast<DeviceImpl*>(device));
}

Result CommandBufferImpl::reset()
{
    SLANG_CUDA_CTX_SCOPE(getDevice<DeviceImpl>());
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
