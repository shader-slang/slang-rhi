#include "wgpu-command.h"
#include "wgpu-device.h"
#include "wgpu-buffer.h"
#include "wgpu-texture.h"
#include "wgpu-pipeline.h"
#include "wgpu-shader-object.h"
#include "wgpu-utils.h"

#include "../strings.h"

#include "core/deferred.h"


namespace rhi::wgpu {

template<typename T>
inline bool arraysEqual(uint32_t countA, uint32_t countB, const T* a, const T* b)
{
    return (countA == countB) ? std::memcmp(a, b, countA * sizeof(T)) == 0 : false;
}

class CommandRecorder
{
public:
    DeviceImpl* m_device;
    Context& m_ctx;

    WGPUCommandEncoder m_commandEncoder = nullptr;
    WGPURenderPassEncoder m_renderPassEncoder = nullptr;
    WGPUComputePassEncoder m_computePassEncoder = nullptr;

    short_vector<RefPtr<TextureViewImpl>> m_renderTargetViews;
    short_vector<RefPtr<TextureViewImpl>> m_resolveTargetViews;
    RefPtr<TextureViewImpl> m_depthStencilView;

    bool m_renderPassActive = false;
    bool m_renderStateValid = false;
    RenderState m_renderState;
    RefPtr<RenderPipelineImpl> m_renderPipeline;

    bool m_computePassActive = false;
    bool m_computeStateValid = false;
    RefPtr<ComputePipelineImpl> m_computePipeline;

    BindingDataImpl* m_bindingData = nullptr;

    CommandRecorder(DeviceImpl* device)
        : m_device(device)
        , m_ctx(device->m_ctx)
    {
    }

    Result record(CommandBufferImpl* commandBuffer);

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

    void endPassEncoder();
};

Result CommandRecorder::record(CommandBufferImpl* commandBuffer)
{
    auto existingError = m_device->getAndClearLastUncapturedError();
    if (existingError != WGPUErrorType_NoError)
        m_device->printWarning("Web GPU device had reported error before command record.");

    m_commandEncoder = m_ctx.api.wgpuDeviceCreateCommandEncoder(m_ctx.device, nullptr);
    SLANG_RHI_DEFERRED({ m_ctx.api.wgpuCommandEncoderRelease(m_commandEncoder); });
    if (!m_commandEncoder)
    {
        return SLANG_FAIL;
    }

    // Upload constant buffer data
    commandBuffer->m_constantBufferPool.upload(m_ctx, m_commandEncoder);

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

    endPassEncoder();

    commandBuffer->m_commandBuffer = m_ctx.api.wgpuCommandEncoderFinish(m_commandEncoder, nullptr);
    if (!commandBuffer->m_commandBuffer)
    {
        return SLANG_FAIL;
    }

    auto lastError = m_device->getAndClearLastUncapturedError();
    if (lastError != WGPUErrorType_NoError)
    {
        return SLANG_FAIL;
    }

    return SLANG_OK;
}

#define NOT_SUPPORTED(x) m_device->printWarning(x " command is not supported!")

void CommandRecorder::cmdCopyBuffer(const commands::CopyBuffer& cmd)
{
    BufferImpl* dst = checked_cast<BufferImpl*>(cmd.dst);
    BufferImpl* src = checked_cast<BufferImpl*>(cmd.src);
    m_ctx.api.wgpuCommandEncoderCopyBufferToBuffer(
        m_commandEncoder,
        src->m_buffer,
        cmd.srcOffset,
        dst->m_buffer,
        cmd.dstOffset,
        cmd.size
    );
}

void CommandRecorder::cmdCopyTexture(const commands::CopyTexture& cmd)
{
    TextureImpl* dst = checked_cast<TextureImpl*>(cmd.dst);
    TextureImpl* src = checked_cast<TextureImpl*>(cmd.src);

    SubresourceRange dstSubresource = cmd.dstSubresource;
    const Offset3D& dstOffset = cmd.dstOffset;
    SubresourceRange srcSubresource = cmd.srcSubresource;
    const Offset3D& srcOffset = cmd.srcOffset;
    const Extent3D& extent = cmd.extent;

    // Fix up sub resource ranges.
    if (dstSubresource.layerCount == 0)
        dstSubresource.layerCount = dst->m_desc.getLayerCount();
    if (dstSubresource.mipCount == 0)
        dstSubresource.mipCount = dst->m_desc.mipCount;
    if (srcSubresource.layerCount == 0)
        srcSubresource.layerCount = src->m_desc.getLayerCount();
    if (srcSubresource.mipCount == 0)
        srcSubresource.mipCount = src->m_desc.mipCount;

    // Validate subresource ranges
    SLANG_RHI_ASSERT(srcSubresource.layer + srcSubresource.layerCount <= src->m_desc.getLayerCount());
    SLANG_RHI_ASSERT(dstSubresource.layer + dstSubresource.layerCount <= dst->m_desc.getLayerCount());
    SLANG_RHI_ASSERT(srcSubresource.mip + srcSubresource.mipCount <= src->m_desc.mipCount);
    SLANG_RHI_ASSERT(dstSubresource.mip + dstSubresource.mipCount <= dst->m_desc.mipCount);

    // Validate matching dimensions between source and destination
    SLANG_RHI_ASSERT(srcSubresource.layerCount == dstSubresource.layerCount);
    SLANG_RHI_ASSERT(srcSubresource.mipCount == dstSubresource.mipCount);

    Extent3D srcTextureSize = src->m_desc.size;
    const FormatInfo& srcFormatInfo = getFormatInfo(src->m_desc.format);
    const FormatInfo& dstFormatInfo = getFormatInfo(dst->m_desc.format);

    for (uint32_t layer = 0; layer < dstSubresource.layerCount; layer++)
    {
        for (uint32_t mip = 0; mip < dstSubresource.mipCount; mip++)
        {
            uint32_t srcMip = srcSubresource.mip + mip;
            uint32_t dstMip = dstSubresource.mip + mip;

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

            // Validate source and destination parameters
            SLANG_RHI_ASSERT(srcOffset.x + adjustedExtent.width <= srcMipSize.width);
            SLANG_RHI_ASSERT(srcOffset.y + adjustedExtent.height <= srcMipSize.height);
            SLANG_RHI_ASSERT(srcOffset.z + adjustedExtent.depth <= srcMipSize.depth);

            // z is either base array layer or z offset depending on whether this is 3D or array texture
            SLANG_RHI_ASSERT(cmd.srcSubresource.layer == 0 || cmd.srcOffset.z == 0);
            SLANG_RHI_ASSERT(cmd.dstSubresource.layer == 0 || cmd.dstOffset.z == 0);
            uint32_t srcZ = cmd.srcOffset.z + cmd.srcSubresource.layer + layer;
            uint32_t dstZ = cmd.dstOffset.z + cmd.dstSubresource.layer + layer;

            WGPUTexelCopyTextureInfo source = {};
            source.texture = src->m_texture;
            source.origin = {cmd.srcOffset.x, cmd.srcOffset.y, srcZ};
            source.mipLevel = srcMip;
            source.aspect = WGPUTextureAspect_All;

            WGPUTexelCopyTextureInfo destination = {};
            destination.texture = dst->m_texture;
            destination.origin = {cmd.dstOffset.x, cmd.dstOffset.y, dstZ};
            destination.mipLevel = dstMip;
            destination.aspect = WGPUTextureAspect_All;

            WGPUExtent3D copySize = {adjustedExtent.width, adjustedExtent.height, adjustedExtent.depth};

            // Align copy sizes to format block dimensions
            copySize.width = math::calcAligned2(copySize.width, srcFormatInfo.blockWidth);
            copySize.height = math::calcAligned2(copySize.height, srcFormatInfo.blockHeight);
            copySize.width = math::calcAligned2(copySize.width, dstFormatInfo.blockWidth);
            copySize.height = math::calcAligned2(copySize.height, dstFormatInfo.blockHeight);

            m_ctx.api.wgpuCommandEncoderCopyTextureToTexture(m_commandEncoder, &source, &destination, &copySize);
        }
    }
}

void CommandRecorder::cmdCopyTextureToBuffer(const commands::CopyTextureToBuffer& cmd)
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

    WGPUTexelCopyTextureInfo source = {};
    source.texture = src->m_texture;
    source.origin = {srcOffset.x, srcOffset.y, z};
    source.mipLevel = srcMip;
    source.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo destination = {};
    destination.buffer = dst->m_buffer;
    destination.layout.offset = dstOffset;
    destination.layout.bytesPerRow = dstRowPitch;

    // This is the DESTINATION rows per image, so is calculated
    // based on the extent being copied, not the the height of the image.
    destination.layout.rowsPerImage = math::divideRoundedUp(adjustedExtent.height, formatInfo.blockHeight);

    // Store copy size.
    WGPUExtent3D copySize = {adjustedExtent.width, adjustedExtent.height, adjustedExtent.depth};

    m_ctx.api.wgpuCommandEncoderCopyTextureToBuffer(m_commandEncoder, &source, &destination, &copySize);
}

void CommandRecorder::cmdClearBuffer(const commands::ClearBuffer& cmd)
{
    BufferImpl* buffer = checked_cast<BufferImpl*>(cmd.buffer);
    m_ctx.api.wgpuCommandEncoderClearBuffer(m_commandEncoder, buffer->m_buffer, cmd.range.offset, cmd.range.size);
}

void CommandRecorder::cmdClearTextureFloat(const commands::ClearTextureFloat& cmd)
{
    NOT_SUPPORTED(S_CommandEncoder_clearTextureFloat);
}

void CommandRecorder::cmdClearTextureUint(const commands::ClearTextureUint& cmd)
{
    NOT_SUPPORTED(S_CommandEncoder_clearTextureUint);
}

void CommandRecorder::cmdClearTextureDepthStencil(const commands::ClearTextureDepthStencil& cmd)
{
    NOT_SUPPORTED(S_CommandEncoder_clearTextureDepthStencil);
}

void CommandRecorder::cmdUploadTextureData(const commands::UploadTextureData& cmd)
{
    auto dst = checked_cast<TextureImpl*>(cmd.dst);
    SubresourceRange subresourceRange = cmd.subresourceRange;

    SubresourceLayout* srLayout = cmd.layouts;
    Offset bufferOffset = cmd.srcOffset;
    auto buffer = checked_cast<BufferImpl*>(cmd.srcBuffer);

    for (uint32_t layerOffset = 0; layerOffset < subresourceRange.layerCount; layerOffset++)
    {
        uint32_t layer = subresourceRange.layer + layerOffset;
        for (uint32_t mipOffset = 0; mipOffset < subresourceRange.mipCount; mipOffset++)
        {
            uint32_t mip = subresourceRange.mip + mipOffset;

            WGPUTexelCopyBufferInfo srcRegion;
            srcRegion.buffer = buffer->m_buffer;
            srcRegion.layout.bytesPerRow = srLayout->rowPitch;
            srcRegion.layout.rowsPerImage = srLayout->rowCount;

            srcRegion.layout.offset = bufferOffset;

            // Can't be copying multiple layers from a volume texture
            SLANG_RHI_ASSERT(layer == 0 || cmd.offset.z == 0);
            uint32_t z = cmd.offset.z + layer;

            WGPUTexelCopyTextureInfo dstRegion;
            dstRegion.aspect = WGPUTextureAspect_All;
            dstRegion.mipLevel = mip;
            dstRegion.origin = {(uint32_t)cmd.offset.x, (uint32_t)cmd.offset.y, z};
            dstRegion.texture = dst->m_texture;

            WGPUExtent3D copySize;
            copySize.width = math::calcAligned2(srLayout->size.width, srLayout->blockWidth);
            copySize.height = math::calcAligned2(srLayout->size.height, srLayout->blockHeight);
            copySize.depthOrArrayLayers = srLayout->size.depth;

            m_ctx.api.wgpuCommandEncoderCopyBufferToTexture(m_commandEncoder, &srcRegion, &dstRegion, &copySize);

            bufferOffset += srLayout->sizeInBytes;
            srLayout++;
        }
    }
}

void CommandRecorder::cmdResolveQuery(const commands::ResolveQuery& cmd)
{
    NOT_SUPPORTED(S_CommandEncoder_resolveQuery);
}

void CommandRecorder::cmdBeginRenderPass(const commands::BeginRenderPass& cmd)
{
    const RenderPassDesc& desc = cmd.desc;

    short_vector<WGPURenderPassColorAttachment, 8> colorAttachments(desc.colorAttachmentCount, {});
    for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i)
    {
        const RenderPassColorAttachment& attachmentIn = desc.colorAttachments[i];
        WGPURenderPassColorAttachment& attachment = colorAttachments[i];
        attachment.view = checked_cast<TextureViewImpl*>(attachmentIn.view)->m_textureView;
        attachment.resolveTarget = attachmentIn.resolveTarget
                                       ? checked_cast<TextureViewImpl*>(attachmentIn.resolveTarget)->m_textureView
                                       : nullptr;
        attachment.depthSlice = -1;         // TODO not provided
        attachment.resolveTarget = nullptr; // TODO not provided
        attachment.loadOp = translateLoadOp(attachmentIn.loadOp);
        attachment.storeOp = translateStoreOp(attachmentIn.storeOp);
        attachment.clearValue.r = attachmentIn.clearValue[0];
        attachment.clearValue.g = attachmentIn.clearValue[1];
        attachment.clearValue.b = attachmentIn.clearValue[2];
        attachment.clearValue.a = attachmentIn.clearValue[3];
    }

    WGPURenderPassDepthStencilAttachment depthStencilAttachment = {};
    if (desc.depthStencilAttachment)
    {
        const RenderPassDepthStencilAttachment& attachmentIn = *desc.depthStencilAttachment;
        WGPURenderPassDepthStencilAttachment& attachment = depthStencilAttachment;
        attachment.view = checked_cast<TextureViewImpl*>(attachmentIn.view)->m_textureView;
        attachment.depthLoadOp = translateLoadOp(attachmentIn.depthLoadOp);
        attachment.depthStoreOp = translateStoreOp(attachmentIn.depthStoreOp);
        attachment.depthClearValue = attachmentIn.depthClearValue;
        attachment.depthReadOnly = attachmentIn.depthReadOnly;
        if (getFormatInfo(attachmentIn.view->getDesc().format).hasStencil)
        {
            attachment.stencilLoadOp = translateLoadOp(attachmentIn.stencilLoadOp);
            attachment.stencilStoreOp = translateStoreOp(attachmentIn.stencilStoreOp);
            attachment.stencilClearValue = attachmentIn.stencilClearValue;
            attachment.stencilReadOnly = attachmentIn.stencilReadOnly;
        }
    }

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = desc.colorAttachmentCount;
    passDesc.colorAttachments = colorAttachments.data();
    passDesc.depthStencilAttachment = desc.depthStencilAttachment ? &depthStencilAttachment : nullptr;
    // passDesc.occlusionQuerySet not supported
    // passDesc.timestampWrites not supported

    endPassEncoder();
    m_renderPassEncoder = m_ctx.api.wgpuCommandEncoderBeginRenderPass(m_commandEncoder, &passDesc);
    m_renderPassActive = true;
}

void CommandRecorder::cmdEndRenderPass(const commands::EndRenderPass& cmd)
{
    endPassEncoder();
    m_renderPassActive = false;
}

void CommandRecorder::cmdSetRenderState(const commands::SetRenderState& cmd)
{
    if (!m_renderPassEncoder)
        return;

    const RenderState& state = cmd.state;

    bool updatePipeline = !m_renderStateValid || cmd.pipeline != m_renderPipeline;
    bool updateBindings = updatePipeline || cmd.bindingData != m_bindingData;
    bool updateStencilRef = !m_renderStateValid || state.stencilRef != m_renderState.stencilRef;
    bool updateVertexBuffers = !m_renderStateValid || !arraysEqual(
                                                          state.vertexBufferCount,
                                                          m_renderState.vertexBufferCount,
                                                          state.vertexBuffers,
                                                          m_renderState.vertexBuffers
                                                      );
    bool updateIndexBuffer = !m_renderStateValid || state.indexFormat != m_renderState.indexFormat ||
                             state.indexBuffer != m_renderState.indexBuffer;
    bool updateViewports =
        !m_renderStateValid ||
        !arraysEqual(state.viewportCount, m_renderState.viewportCount, state.viewports, m_renderState.viewports);
    bool updateScissorRects = !m_renderStateValid || !arraysEqual(
                                                         state.scissorRectCount,
                                                         m_renderState.scissorRectCount,
                                                         state.scissorRects,
                                                         m_renderState.scissorRects
                                                     );

    WGPURenderPassEncoder encoder = m_renderPassEncoder;

    if (updatePipeline)
    {
        m_renderPipeline = checked_cast<RenderPipelineImpl*>(cmd.pipeline);
        m_ctx.api.wgpuRenderPassEncoderSetPipeline(encoder, m_renderPipeline->m_renderPipeline);
    }

    if (updateBindings)
    {
        m_bindingData = static_cast<BindingDataImpl*>(cmd.bindingData);
        for (uint32_t groupIndex = 0; groupIndex < m_bindingData->bindGroupCount; groupIndex++)
        {
            m_ctx.api.wgpuRenderPassEncoderSetBindGroup(
                m_renderPassEncoder,
                groupIndex,
                m_bindingData->bindGroups[groupIndex],
                0,
                nullptr
            );
        }
    }

    if (updateStencilRef)
    {
        m_ctx.api.wgpuRenderPassEncoderSetStencilReference(m_renderPassEncoder, state.stencilRef);
    }

    if (updateVertexBuffers)
    {
        for (uint32_t i = 0; i < state.vertexBufferCount; ++i)
        {
            BufferImpl* buffer = checked_cast<BufferImpl*>(state.vertexBuffers[i].buffer);
            uint64_t offset = state.vertexBuffers[i].offset;
            m_ctx.api.wgpuRenderPassEncoderSetVertexBuffer(
                m_renderPassEncoder,
                i,
                buffer->m_buffer,
                offset,
                buffer->m_desc.size - offset
            );
        }
    }

    if (updateIndexBuffer)
    {
        if (state.indexBuffer.buffer)
        {
            BufferImpl* buffer = checked_cast<BufferImpl*>(state.indexBuffer.buffer);
            uint64_t offset = state.indexBuffer.offset;
            m_ctx.api.wgpuRenderPassEncoderSetIndexBuffer(
                m_renderPassEncoder,
                buffer->m_buffer,
                state.indexFormat == IndexFormat::Uint32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16,
                offset,
                buffer->m_desc.size - offset
            );
        }
    }

    if (updateViewports && state.viewportCount > 0)
    {
        const Viewport& viewport = state.viewports[0];
        m_ctx.api.wgpuRenderPassEncoderSetViewport(
            m_renderPassEncoder,
            viewport.originX,
            viewport.originY,
            viewport.extentX,
            viewport.extentY,
            viewport.minZ,
            viewport.maxZ
        );
    }

    if (updateScissorRects && state.scissorRectCount > 0)
    {
        const ScissorRect& scissorRect = state.scissorRects[0];
        m_ctx.api.wgpuRenderPassEncoderSetScissorRect(
            m_renderPassEncoder,
            scissorRect.minX,
            scissorRect.minY,
            scissorRect.maxX - scissorRect.minX,
            scissorRect.maxY - scissorRect.minY
        );
    }

    m_renderStateValid = true;
    m_renderState = state;

    m_computeStateValid = false;
    m_computePipeline = nullptr;
}

void CommandRecorder::cmdDraw(const commands::Draw& cmd)
{
    if (!m_renderStateValid)
        return;

    m_ctx.api.wgpuRenderPassEncoderDraw(
        m_renderPassEncoder,
        cmd.args.vertexCount,
        cmd.args.instanceCount,
        cmd.args.startVertexLocation,
        cmd.args.startInstanceLocation
    );
}

void CommandRecorder::cmdDrawIndexed(const commands::DrawIndexed& cmd)
{
    if (!m_renderStateValid)
        return;

    m_ctx.api.wgpuRenderPassEncoderDrawIndexed(
        m_renderPassEncoder,
        cmd.args.vertexCount,
        cmd.args.instanceCount,
        cmd.args.startIndexLocation,
        cmd.args.startVertexLocation,
        cmd.args.startInstanceLocation
    );
}

void CommandRecorder::cmdDrawIndirect(const commands::DrawIndirect& cmd)
{
    if (!m_renderStateValid)
        return;

    m_ctx.api.wgpuRenderPassEncoderMultiDrawIndirect(
        m_renderPassEncoder,
        checked_cast<BufferImpl*>(cmd.argBuffer.buffer)->m_buffer,
        cmd.argBuffer.offset,
        cmd.maxDrawCount,
        cmd.countBuffer ? checked_cast<BufferImpl*>(cmd.countBuffer.buffer)->m_buffer : nullptr,
        cmd.countBuffer.offset
    );
}

void CommandRecorder::cmdDrawIndexedIndirect(const commands::DrawIndexedIndirect& cmd)
{
    if (!m_renderStateValid)
        return;

    m_ctx.api.wgpuRenderPassEncoderMultiDrawIndexedIndirect(
        m_renderPassEncoder,
        checked_cast<BufferImpl*>(cmd.argBuffer.buffer)->m_buffer,
        cmd.argBuffer.offset,
        cmd.maxDrawCount,
        cmd.countBuffer ? checked_cast<BufferImpl*>(cmd.countBuffer.buffer)->m_buffer : nullptr,
        cmd.countBuffer.offset
    );
}

void CommandRecorder::cmdDrawMeshTasks(const commands::DrawMeshTasks& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_RenderPassEncoder_drawMeshTasks);
}

void CommandRecorder::cmdBeginComputePass(const commands::BeginComputePass& cmd)
{
    endPassEncoder();
    m_computePassEncoder = m_ctx.api.wgpuCommandEncoderBeginComputePass(m_commandEncoder, nullptr);
    m_computePassActive = true;
}

void CommandRecorder::cmdEndComputePass(const commands::EndComputePass& cmd)
{
    endPassEncoder();
    m_computePassActive = false;
}

void CommandRecorder::cmdSetComputeState(const commands::SetComputeState& cmd)
{
    if (!m_computePassActive)
        return;

    bool updatePipeline = !m_computeStateValid || cmd.pipeline != m_computePipeline;
    bool updateBindings = updatePipeline || cmd.bindingData != m_bindingData;

    WGPUComputePassEncoder encoder = m_computePassEncoder;

    if (updatePipeline)
    {
        m_computePipeline = checked_cast<ComputePipelineImpl*>(cmd.pipeline);
        m_ctx.api.wgpuComputePassEncoderSetPipeline(encoder, m_computePipeline->m_computePipeline);
    }

    if (updateBindings)
    {
        m_bindingData = static_cast<BindingDataImpl*>(cmd.bindingData);
        for (uint32_t groupIndex = 0; groupIndex < m_bindingData->bindGroupCount; groupIndex++)
        {
            m_ctx.api.wgpuComputePassEncoderSetBindGroup(
                m_computePassEncoder,
                groupIndex,
                m_bindingData->bindGroups[groupIndex],
                0,
                nullptr
            );
        }
    }

    m_computeStateValid = true;
}

void CommandRecorder::cmdDispatchCompute(const commands::DispatchCompute& cmd)
{
    if (!m_computeStateValid)
        return;

    m_ctx.api.wgpuComputePassEncoderDispatchWorkgroups(m_computePassEncoder, cmd.x, cmd.y, cmd.z);
}

void CommandRecorder::cmdDispatchComputeIndirect(const commands::DispatchComputeIndirect& cmd)
{
    if (!m_computeStateValid)
        return;

    m_ctx.api.wgpuComputePassEncoderDispatchWorkgroupsIndirect(
        m_computePassEncoder,
        checked_cast<BufferImpl*>(cmd.argBuffer.buffer)->m_buffer,
        cmd.argBuffer.offset
    );
}

void CommandRecorder::cmdBeginRayTracingPass(const commands::BeginRayTracingPass& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_beginRayTracingPass);
}

void CommandRecorder::cmdEndRayTracingPass(const commands::EndRayTracingPass& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandRecorder::cmdSetRayTracingState(const commands::SetRayTracingState& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandRecorder::cmdDispatchRays(const commands::DispatchRays& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_RayTracingPassEncoder_dispatchRays);
}

void CommandRecorder::cmdBuildAccelerationStructure(const commands::BuildAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_buildAccelerationStructure);
}

void CommandRecorder::cmdCopyAccelerationStructure(const commands::CopyAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_copyAccelerationStructure);
}

void CommandRecorder::cmdQueryAccelerationStructureProperties(const commands::QueryAccelerationStructureProperties& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_queryAccelerationStructureProperties);
}

void CommandRecorder::cmdSerializeAccelerationStructure(const commands::SerializeAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_serializeAccelerationStructure);
}

void CommandRecorder::cmdDeserializeAccelerationStructure(const commands::DeserializeAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_deserializeAccelerationStructure);
}

void CommandRecorder::cmdExecuteClusterOperation(const commands::ExecuteClusterOperation& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_executeClusterOperation);
}

void CommandRecorder::cmdConvertCooperativeVectorMatrix(const commands::ConvertCooperativeVectorMatrix& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_convertCooperativeVectorMatrix);
}

void CommandRecorder::cmdSetBufferState(const commands::SetBufferState& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandRecorder::cmdSetTextureState(const commands::SetTextureState& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandRecorder::cmdGlobalBarrier(const commands::GlobalBarrier& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandRecorder::cmdPushDebugGroup(const commands::PushDebugGroup& cmd)
{
    if (m_renderPassActive)
    {
        m_ctx.api.wgpuRenderPassEncoderPushDebugGroup(m_renderPassEncoder, translateString(cmd.name));
    }
    else if (m_computePassActive)
    {
        m_ctx.api.wgpuComputePassEncoderPushDebugGroup(m_computePassEncoder, translateString(cmd.name));
    }
    else
    {
        m_ctx.api.wgpuCommandEncoderPushDebugGroup(m_commandEncoder, translateString(cmd.name));
    }
}

void CommandRecorder::cmdPopDebugGroup(const commands::PopDebugGroup& cmd)
{
    if (m_renderPassActive)
    {
        m_ctx.api.wgpuRenderPassEncoderPopDebugGroup(m_renderPassEncoder);
    }
    else if (m_computePassActive)
    {
        m_ctx.api.wgpuComputePassEncoderPopDebugGroup(m_computePassEncoder);
    }
    else
    {
        m_ctx.api.wgpuCommandEncoderPopDebugGroup(m_commandEncoder);
    }
}

void CommandRecorder::cmdInsertDebugMarker(const commands::InsertDebugMarker& cmd)
{
    if (m_renderPassActive)
    {
        m_ctx.api.wgpuRenderPassEncoderInsertDebugMarker(m_renderPassEncoder, translateString(cmd.name));
    }
    else if (m_computePassActive)
    {
        m_ctx.api.wgpuComputePassEncoderInsertDebugMarker(m_computePassEncoder, translateString(cmd.name));
    }
    else
    {
        m_ctx.api.wgpuCommandEncoderInsertDebugMarker(m_commandEncoder, translateString(cmd.name));
    }
}

void CommandRecorder::cmdWriteTimestamp(const commands::WriteTimestamp& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_writeTimestamp);
}

void CommandRecorder::cmdExecuteCallback(const commands::ExecuteCallback& cmd)
{
    cmd.callback(cmd.userData);
}

void CommandRecorder::endPassEncoder()
{
    if (m_renderPassEncoder)
    {
        m_device->m_ctx.api.wgpuRenderPassEncoderEnd(m_renderPassEncoder);
        m_device->m_ctx.api.wgpuRenderPassEncoderRelease(m_renderPassEncoder);
        m_renderPassEncoder = nullptr;

        m_renderStateValid = false;
        m_renderState = {};
        m_renderPipeline = nullptr;
    }
    if (m_computePassEncoder)
    {
        m_device->m_ctx.api.wgpuComputePassEncoderEnd(m_computePassEncoder);
        m_device->m_ctx.api.wgpuComputePassEncoderRelease(m_computePassEncoder);
        m_computePassEncoder = nullptr;

        m_computeStateValid = false;
        m_computePipeline = nullptr;
    }

    m_bindingData = nullptr;
}

// CommandQueueImpl

CommandQueueImpl::CommandQueueImpl(Device* device_, QueueType type)
    : CommandQueue(device_, type)
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    m_queue = device->m_ctx.api.wgpuDeviceGetQueue(device->m_ctx.device);
}

CommandQueueImpl::~CommandQueueImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    if (m_queue)
    {
        device->m_ctx.api.wgpuQueueRelease(m_queue);
    }
}

Result CommandQueueImpl::createCommandEncoder(ICommandEncoder** outEncoder)
{
    RefPtr<CommandEncoderImpl> encoder = new CommandEncoderImpl(m_device, this);
    returnComPtr(outEncoder, encoder);
    return SLANG_OK;
}

Result CommandQueueImpl::submit(const SubmitDesc& desc)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

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

    // Submit command buffers.
    short_vector<WGPUCommandBuffer, 16> commandBuffers;
    for (uint32_t i = 0; i < desc.commandBufferCount; i++)
    {
        commandBuffers.push_back(checked_cast<CommandBufferImpl*>(desc.commandBuffers[i])->m_commandBuffer);
    }
    device->m_ctx.api.wgpuQueueSubmit(m_queue, commandBuffers.size(), commandBuffers.data());

    // Signal fences.
    for (uint32_t i = 0; i < desc.signalFenceCount; ++i)
    {
        SLANG_RETURN_ON_FAIL(desc.signalFences[i]->setCurrentValue(desc.signalFenceValues[i]));
    }

    return SLANG_OK;
}

Result CommandQueueImpl::waitOnHost()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    // Wait for the command buffer to finish executing
    {
        WGPUQueueWorkDoneStatus status = WGPUQueueWorkDoneStatus(0);
        WGPUQueueWorkDoneCallbackInfo callbackInfo = {};
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = [](WGPUQueueWorkDoneStatus status_, void* userdata1, void* userdata2)
        {
            *(WGPUQueueWorkDoneStatus*)userdata1 = status_;
        };
        callbackInfo.userdata1 = &status;
        WGPUFuture future = device->m_ctx.api.wgpuQueueOnSubmittedWorkDone(m_queue, callbackInfo);
        constexpr size_t futureCount = 1;
        WGPUFutureWaitInfo futures[futureCount] = {{future}};
        uint64_t timeoutNS = UINT64_MAX;
        WGPUWaitStatus waitStatus =
            device->m_ctx.api.wgpuInstanceWaitAny(device->m_ctx.instance, futureCount, futures, timeoutNS);
        if (waitStatus != WGPUWaitStatus_Success || status != WGPUQueueWorkDoneStatus_Success)
        {
            return SLANG_FAIL;
        }
    }
    return SLANG_OK;
}

Result CommandQueueImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::WGPUQueue;
    outHandle->value = (uint64_t)m_queue;
    return SLANG_OK;
}

Result DeviceImpl::getQueue(QueueType type, ICommandQueue** outQueue)
{
    if (type != QueueType::Graphics)
        return SLANG_FAIL;
    m_queue->establishStrongReferenceToDevice();
    returnComPtr(outQueue, m_queue);
    return SLANG_OK;
}

// CommandEncoderImpl

CommandEncoderImpl::CommandEncoderImpl(Device* device, CommandQueueImpl* queue)
    : CommandEncoder(device)
    , m_queue(queue)
{
    m_commandBuffer = new CommandBufferImpl(device, queue);
    m_commandList = &m_commandBuffer->m_commandList;
}

CommandEncoderImpl::~CommandEncoderImpl() {}

Result CommandEncoderImpl::getBindingData(RootShaderObject* rootObject, BindingData*& outBindingData)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    rootObject->trackResources(m_commandBuffer->m_trackedObjects);
    BindingDataBuilder builder;
    builder.m_device = device;
    builder.m_commandList = m_commandList;
    builder.m_constantBufferPool = &m_commandBuffer->m_constantBufferPool;
    builder.m_allocator = &m_commandBuffer->m_allocator;
    builder.m_bindingCache = &m_commandBuffer->m_bindingCache;
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
    m_commandBuffer->m_constantBufferPool.finish();
    CommandRecorder recorder(getDevice<DeviceImpl>());
    SLANG_RETURN_ON_FAIL(recorder.record(m_commandBuffer));
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

CommandBufferImpl::CommandBufferImpl(Device* device_, CommandQueueImpl* queue)
    : CommandBuffer(device_)
    , m_queue(queue)
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    m_constantBufferPool.init(device);
}

CommandBufferImpl::~CommandBufferImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    reset();
    if (m_commandBuffer)
    {
        device->m_ctx.api.wgpuCommandBufferRelease(m_commandBuffer);
    }
}

Result CommandBufferImpl::reset()
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    m_constantBufferPool.reset();
    m_bindingCache.reset(device);
    return CommandBuffer::reset();
}

Result CommandBufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::WGPUCommandBuffer;
    outHandle->value = (uint64_t)m_commandBuffer;
    return SLANG_OK;
}

} // namespace rhi::wgpu
