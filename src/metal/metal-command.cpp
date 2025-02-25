#include "metal-command.h"
#include "metal-device.h"
#include "metal-buffer.h"
#include "metal-texture.h"
#include "metal-query.h"
#include "metal-fence.h"
#include "metal-pipeline.h"
#include "metal-acceleration-structure.h"
#include "metal-shader-object.h"
#include "metal-helper-functions.h"
#include "metal-util.h"
#include "../strings.h"

namespace rhi::metal {

template<typename T>
inline bool arraysEqual(uint32_t countA, uint32_t countB, const T* a, const T* b)
{
    return (countA == countB) ? std::memcmp(a, b, countA * sizeof(T)) == 0 : false;
}

class CommandRecorder
{
public:
    DeviceImpl* m_device;

    NS::SharedPtr<MTL::CommandBuffer> m_commandBuffer;
    NS::SharedPtr<MTL::RenderCommandEncoder> m_renderCommandEncoder;
    NS::SharedPtr<MTL::ComputeCommandEncoder> m_computeCommandEncoder;
    NS::SharedPtr<MTL::AccelerationStructureCommandEncoder> m_accelerationStructureCommandEncoder;
    NS::SharedPtr<MTL::BlitCommandEncoder> m_blitCommandEncoder;

    short_vector<RefPtr<TextureViewImpl>> m_renderTargetViews;
    short_vector<RefPtr<TextureViewImpl>> m_resolveTargetViews;
    RefPtr<TextureViewImpl> m_depthStencilView;

    bool m_renderPassActive = false;
    bool m_renderStateValid = false;
    RenderState m_renderState;
    RefPtr<RenderPipelineImpl> m_renderPipeline;
    bool m_useDepthStencil = false;
    RefPtr<BufferImpl> m_indexBuffer;
    MTL::IndexType m_indexType;
    NS::UInteger m_indexSize;
    NS::UInteger m_indexBufferOffset;

    bool m_computePassActive = false;
    bool m_computeStateValid = false;
    RefPtr<ComputePipelineImpl> m_computePipeline;

    bool m_rayTracingPassActive = false;
    bool m_rayTracingStateValid = false;
    RefPtr<RayTracingPipelineImpl> m_rayTracingPipeline;

    BindingDataImpl* m_bindingData = nullptr;

    CommandRecorder(DeviceImpl* device)
        : m_device(device)
    {
    }

    Result record(CommandBufferImpl* commandBuffer);

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

    MTL::RenderCommandEncoder* getRenderCommandEncoder(MTL::RenderPassDescriptor* renderPassDesc);
    MTL::ComputeCommandEncoder* getComputeCommandEncoder();
    MTL::AccelerationStructureCommandEncoder* getAccelerationStructureCommandEncoder();
    MTL::BlitCommandEncoder* getBlitCommandEncoder();
    void endCommandEncoder();
};

Result CommandRecorder::record(CommandBufferImpl* commandBuffer)
{
    m_commandBuffer = commandBuffer->m_commandBuffer;

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

    endCommandEncoder();

    return SLANG_OK;
}

#define NOT_SUPPORTED(x) m_device->warning(x " command is not supported!")

void CommandRecorder::cmdCopyBuffer(const commands::CopyBuffer& cmd)
{
    BufferImpl* dst = checked_cast<BufferImpl*>(cmd.dst);
    BufferImpl* src = checked_cast<BufferImpl*>(cmd.src);

    auto encoder = getBlitCommandEncoder();
    encoder->copyFromBuffer(src->m_buffer.get(), cmd.srcOffset, dst->m_buffer.get(), cmd.dstOffset, cmd.size);
}

void CommandRecorder::cmdCopyTexture(const commands::CopyTexture& cmd)
{
    TextureImpl* src = checked_cast<TextureImpl*>(cmd.src);
    TextureImpl* dst = checked_cast<TextureImpl*>(cmd.dst);

    const SubresourceRange& srcSubresource = cmd.srcSubresource;
    const SubresourceRange& dstSubresource = cmd.dstSubresource;
    const Offset3D& srcOffset = cmd.srcOffset;
    const Offset3D& dstOffset = cmd.dstOffset;
    const Extents& extent = cmd.extent;

    auto encoder = getBlitCommandEncoder();

    if (dstSubresource.layerCount == 0 && dstSubresource.mipLevelCount == 0 && srcSubresource.layerCount == 0 &&
        srcSubresource.mipLevelCount == 0)
    {
        encoder->copyFromTexture(src->m_texture.get(), dst->m_texture.get());
    }
    else
    {
        for (uint32_t layer = 0; layer < dstSubresource.layerCount; layer++)
        {
            encoder->copyFromTexture(
                src->m_texture.get(),
                srcSubresource.baseArrayLayer + layer,
                srcSubresource.mipLevel,
                MTL::Origin(srcOffset.x, srcOffset.y, srcOffset.z),
                MTL::Size(extent.width, extent.height, extent.depth),
                dst->m_texture.get(),
                dstSubresource.baseArrayLayer + layer,
                dstSubresource.mipLevel,
                MTL::Origin(dstOffset.x, dstOffset.y, dstOffset.z)
            );
        }
    }
}

void CommandRecorder::cmdCopyTextureToBuffer(const commands::CopyTextureToBuffer& cmd)
{
    TextureImpl* src = checked_cast<TextureImpl*>(cmd.src);
    BufferImpl* dst = checked_cast<BufferImpl*>(cmd.dst);

    const SubresourceRange& srcSubresource = cmd.srcSubresource;
    const Offset3D& srcOffset = cmd.srcOffset;
    const Extents& extent = cmd.extent;

    SLANG_RHI_ASSERT(srcSubresource.mipLevelCount <= 1);

    auto encoder = getBlitCommandEncoder();
    encoder->copyFromTexture(
        src->m_texture.get(),
        srcSubresource.baseArrayLayer,
        srcSubresource.mipLevel,
        MTL::Origin(srcOffset.x, srcOffset.y, srcOffset.z),
        MTL::Size(extent.width, extent.height, extent.depth),
        dst->m_buffer.get(),
        cmd.dstOffset,
        cmd.dstRowStride,
        cmd.dstRowStride * extent.height
    );
}

void CommandRecorder::cmdClearBuffer(const commands::ClearBuffer& cmd)
{
    NOT_SUPPORTED(S_CommandEncoder_clearBuffer);
}

void CommandRecorder::cmdClearTexture(const commands::ClearTexture& cmd)
{
    NOT_SUPPORTED(S_CommandEncoder_clearTexture);
}

void CommandRecorder::cmdUploadTextureData(const commands::UploadTextureData& cmd)
{
    NOT_SUPPORTED(S_CommandEncoder_uploadTextureData);
}

void CommandRecorder::cmdUploadBufferData(const commands::UploadBufferData& cmd)
{
    NOT_SUPPORTED(S_CommandEncoder_uploadBufferData);
}

void CommandRecorder::cmdResolveQuery(const commands::ResolveQuery& cmd)
{
    QueryPoolImpl* queryPool = checked_cast<QueryPoolImpl*>(cmd.queryPool);
    BufferImpl* buffer = checked_cast<BufferImpl*>(cmd.buffer);

    auto encoder = getBlitCommandEncoder();
    encoder->resolveCounters(
        queryPool->m_counterSampleBuffer.get(),
        NS::Range(cmd.index, cmd.count),
        buffer->m_buffer.get(),
        cmd.offset
    );
}

void CommandRecorder::cmdBeginRenderPass(const commands::BeginRenderPass& cmd)
{
    const RenderPassDesc& desc = cmd.desc;

    uint32_t width = 1;
    uint32_t height = 1;

    auto visitView = [&](TextureViewImpl* view)
    {
        const TextureDesc& textureDesc = view->m_texture->m_desc;
        const TextureViewDesc& viewDesc = view->m_desc;
        width = max(1u, uint32_t(textureDesc.size.width >> viewDesc.subresourceRange.mipLevel));
        height = max(1u, uint32_t(textureDesc.size.height >> viewDesc.subresourceRange.mipLevel));
    };

    // Initialize render pass descriptor.
    NS::SharedPtr<MTL::RenderPassDescriptor> renderPassDesc =
        NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());

    // Setup color attachments.
    renderPassDesc->setRenderTargetArrayLength(desc.colorAttachmentCount);
    for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i)
    {
        const auto& attachment = desc.colorAttachments[i];
        TextureViewImpl* view = checked_cast<TextureViewImpl*>(attachment.view);
        if (!view)
            return;
        visitView(view);

        MTL::RenderPassColorAttachmentDescriptor* colorAttachment = renderPassDesc->colorAttachments()->object(i);
        colorAttachment->setLoadAction(MetalUtil::translateLoadOp(attachment.loadOp));
        colorAttachment->setStoreAction(
            MetalUtil::translateStoreOp(attachment.storeOp, attachment.resolveTarget != nullptr)
        );
        if (attachment.loadOp == LoadOp::Clear)
        {
            colorAttachment->setClearColor(MTL::ClearColor(
                attachment.clearValue[0],
                attachment.clearValue[1],
                attachment.clearValue[2],
                attachment.clearValue[3]
            ));
        }
        colorAttachment->setTexture(view->m_textureView.get());
        colorAttachment->setResolveTexture(
            attachment.resolveTarget ? checked_cast<TextureViewImpl*>(attachment.resolveTarget)->m_textureView.get()
                                     : nullptr
        );
        colorAttachment->setLevel(view->m_desc.subresourceRange.mipLevel);
        colorAttachment->setSlice(view->m_desc.subresourceRange.baseArrayLayer);
    }

    // Setup depth stencil attachment.
    if (desc.depthStencilAttachment)
    {
        const auto& attachment = *desc.depthStencilAttachment;
        TextureViewImpl* view = checked_cast<TextureViewImpl*>(attachment.view);
        if (!view)
            return;
        visitView(view);

        MTL::PixelFormat pixelFormat = MetalUtil::translatePixelFormat(view->m_desc.format);
        if (MetalUtil::isDepthFormat(pixelFormat))
        {
            MTL::RenderPassDepthAttachmentDescriptor* depthAttachment = renderPassDesc->depthAttachment();
            depthAttachment->setLoadAction(MetalUtil::translateLoadOp(attachment.depthLoadOp));
            depthAttachment->setStoreAction(MetalUtil::translateStoreOp(attachment.depthStoreOp, false));
            if (attachment.depthLoadOp == LoadOp::Clear)
            {
                depthAttachment->setClearDepth(attachment.depthClearValue);
            }
            depthAttachment->setTexture(view->m_textureView.get());
            depthAttachment->setLevel(view->m_desc.subresourceRange.mipLevel);
            depthAttachment->setSlice(view->m_desc.subresourceRange.baseArrayLayer);
        }
        if (MetalUtil::isStencilFormat(pixelFormat))
        {
            MTL::RenderPassStencilAttachmentDescriptor* stencilAttachment = renderPassDesc->stencilAttachment();
            stencilAttachment->setLoadAction(MetalUtil::translateLoadOp(attachment.stencilLoadOp));
            stencilAttachment->setStoreAction(MetalUtil::translateStoreOp(attachment.stencilStoreOp, false));
            if (attachment.stencilLoadOp == LoadOp::Clear)
            {
                stencilAttachment->setClearStencil(attachment.stencilClearValue);
            }
            stencilAttachment->setTexture(view->m_textureView.get());
            stencilAttachment->setLevel(view->m_desc.subresourceRange.mipLevel);
            stencilAttachment->setSlice(view->m_desc.subresourceRange.baseArrayLayer);
        }
    }

    renderPassDesc->setRenderTargetWidth(width);
    renderPassDesc->setRenderTargetHeight(height);

    m_useDepthStencil = desc.depthStencilAttachment != nullptr;

    getRenderCommandEncoder(renderPassDesc.get());

    m_renderPassActive = true;
}

void CommandRecorder::cmdEndRenderPass(const commands::EndRenderPass& cmd)
{
    endCommandEncoder();

    m_renderPassActive = false;
}

void CommandRecorder::cmdSetRenderState(const commands::SetRenderState& cmd)
{
    if (!m_renderPassActive)
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

    MTL::RenderCommandEncoder* encoder = m_renderCommandEncoder.get();

    if (updatePipeline)
    {
        m_renderPipeline = checked_cast<RenderPipelineImpl*>(cmd.pipeline);
        encoder->setRenderPipelineState(m_renderPipeline->m_pipelineState.get());
    }

    if (updateBindings)
    {
        m_bindingData = static_cast<BindingDataImpl*>(cmd.bindingData);
        encoder->setVertexBuffers(
            m_bindingData->buffers,
            m_bindingData->bufferOffsets,
            NS::Range(0, m_bindingData->bufferCount)
        );
        encoder->setFragmentBuffers(
            m_bindingData->buffers,
            m_bindingData->bufferOffsets,
            NS::Range(0, m_bindingData->bufferCount)
        );
        encoder->setVertexTextures(m_bindingData->textures, NS::Range(0, m_bindingData->textureCount));
        encoder->setFragmentTextures(m_bindingData->textures, NS::Range(0, m_bindingData->textureCount));
        encoder->setVertexSamplerStates(m_bindingData->samplers, NS::Range(0, m_bindingData->samplerCount));
        encoder->setFragmentSamplerStates(m_bindingData->samplers, NS::Range(0, m_bindingData->samplerCount));
    }

    if (updateVertexBuffers)
    {
        for (uint32_t i = 0; i < state.vertexBufferCount; ++i)
        {
            BufferImpl* buffer = checked_cast<BufferImpl*>(state.vertexBuffers[i].buffer);
            encoder->setVertexBuffer(
                buffer->m_buffer.get(),
                state.vertexBuffers[i].offset,
                m_renderPipeline->m_vertexBufferOffset + i
            );
        }
    }

    if (updateIndexBuffer)
    {
        if (state.indexBuffer)
        {
            m_indexBuffer = checked_cast<BufferImpl*>(state.indexBuffer.buffer);
            m_indexBufferOffset = state.indexBuffer.offset;
            switch (state.indexFormat)
            {
            case IndexFormat::UInt16:
                m_indexType = MTL::IndexTypeUInt16;
                m_indexSize = 2;
                break;
            case IndexFormat::UInt32:
                m_indexType = MTL::IndexTypeUInt32;
                m_indexSize = 4;
                break;
            }
        }
    }

    if (updateViewports)
    {
        MTL::Viewport viewports[SLANG_COUNT_OF(RenderState::viewports)];
        for (uint32_t i = 0; i < state.viewportCount; ++i)
        {
            const Viewport& src = state.viewports[i];
            MTL::Viewport& dst = viewports[i];
            dst.originX = src.originX;
            dst.originY = src.originY;
            dst.width = src.extentX;
            dst.height = src.extentY;
            dst.znear = src.minZ;
            dst.zfar = src.maxZ;
        }
        encoder->setViewports(viewports, state.viewportCount);
    }

    if (updateScissorRects)
    {
        MTL::ScissorRect scissorRects[SLANG_COUNT_OF(RenderState::scissorRects)];
        for (uint32_t i = 0; i < state.scissorRectCount; ++i)
        {
            const ScissorRect& src = state.scissorRects[i];
            MTL::ScissorRect& dst = scissorRects[i];
            dst.x = src.minX;
            dst.y = src.minY;
            dst.width = src.maxX - src.minX;
            dst.height = src.maxY - src.minY;
        }
        encoder->setScissorRects(scissorRects, state.scissorRectCount);
    }

    const RasterizerDesc& rasterizer = m_renderPipeline->m_rasterizerDesc;
    encoder->setFrontFacingWinding(MetalUtil::translateWinding(rasterizer.frontFace));
    encoder->setCullMode(MetalUtil::translateCullMode(rasterizer.cullMode));
    encoder->setDepthClipMode(
        rasterizer.depthClipEnable ? MTL::DepthClipModeClip : MTL::DepthClipModeClamp
    ); // TODO correct?
    encoder->setDepthBias(rasterizer.depthBias, rasterizer.slopeScaledDepthBias, rasterizer.depthBiasClamp);
    encoder->setTriangleFillMode(MetalUtil::translateTriangleFillMode(rasterizer.fillMode));
    // encoder->setBlendColor(); // not supported by rhi
    if (m_useDepthStencil)
    {
        encoder->setDepthStencilState(m_renderPipeline->m_depthStencilState.get());
    }

    if (updateStencilRef)
    {
        encoder->setStencilReferenceValue(state.stencilRef);
    }

    m_renderStateValid = true;
    m_renderState = state;
}

void CommandRecorder::cmdDraw(const commands::Draw& cmd)
{
    if (!m_renderStateValid)
        return;

    m_renderCommandEncoder->drawPrimitives(
        m_renderPipeline->m_primitiveType,
        cmd.args.startVertexLocation,
        cmd.args.vertexCount,
        cmd.args.instanceCount,
        cmd.args.startInstanceLocation
    );
}

void CommandRecorder::cmdDrawIndexed(const commands::DrawIndexed& cmd)
{
    if (!m_renderStateValid)
        return;

    m_renderCommandEncoder->drawIndexedPrimitives(
        m_renderPipeline->m_primitiveType,
        cmd.args.vertexCount,
        m_indexType,
        m_indexBuffer->m_buffer.get(),
        m_indexBufferOffset + cmd.args.startIndexLocation * m_indexSize,
        cmd.args.instanceCount,
        cmd.args.startVertexLocation,
        cmd.args.startInstanceLocation
    );
}

void CommandRecorder::cmdDrawIndirect(const commands::DrawIndirect& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_RenderPassEncoder_drawIndirect);
}

void CommandRecorder::cmdDrawIndexedIndirect(const commands::DrawIndexedIndirect& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_RenderPassEncoder_drawIndexedIndirect);
}

void CommandRecorder::cmdDrawMeshTasks(const commands::DrawMeshTasks& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_RenderPassEncoder_drawMeshTasks);
}

void CommandRecorder::cmdBeginComputePass(const commands::BeginComputePass& cmd)
{
    m_computePassActive = true;
}

void CommandRecorder::cmdEndComputePass(const commands::EndComputePass& cmd)
{
    m_computePassActive = false;
}

void CommandRecorder::cmdSetComputeState(const commands::SetComputeState& cmd)
{
    if (!m_computePassActive)
        return;

    bool updatePipeline = !m_computeStateValid || cmd.pipeline != m_computePipeline;
    bool updateBindings = updatePipeline || cmd.bindingData != m_bindingData;

    MTL::ComputeCommandEncoder* encoder = getComputeCommandEncoder();

    if (updatePipeline)
    {
        m_computePipeline = checked_cast<ComputePipelineImpl*>(cmd.pipeline);
        encoder->setComputePipelineState(m_computePipeline->m_pipelineState.get());
    }
    if (updateBindings)
    {
        m_bindingData = static_cast<BindingDataImpl*>(cmd.bindingData);
        encoder->setBuffers(
            m_bindingData->buffers,
            m_bindingData->bufferOffsets,
            NS::Range(0, m_bindingData->bufferCount)
        );
        encoder->setTextures(m_bindingData->textures, NS::Range(0, m_bindingData->textureCount));
        encoder->setSamplerStates(m_bindingData->samplers, NS::Range(0, m_bindingData->samplerCount));
    }

    m_computeStateValid = true;
}

void CommandRecorder::cmdDispatchCompute(const commands::DispatchCompute& cmd)
{
    if (!m_computeStateValid)
        return;

    m_computeCommandEncoder->dispatchThreadgroups(MTL::Size(cmd.x, cmd.y, cmd.z), m_computePipeline->m_threadGroupSize);
}

void CommandRecorder::cmdDispatchComputeIndirect(const commands::DispatchComputeIndirect& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_ComputePassEncoder_dispatchComputeIndirect);
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
    MTL::AccelerationStructureCommandEncoder* encoder = getAccelerationStructureCommandEncoder();

    AccelerationStructureDescBuilder builder;
    builder.build(cmd.desc, m_device->getAccelerationStructureArray(), m_device->m_debugCallback);

    switch (cmd.desc.mode)
    {
    case AccelerationStructureBuildMode::Build:
        encoder->buildAccelerationStructure(
            checked_cast<AccelerationStructureImpl*>(cmd.dst)->m_accelerationStructure.get(),
            builder.descriptor.get(),
            checked_cast<BufferImpl*>(cmd.scratchBuffer.buffer)->m_buffer.get(),
            cmd.scratchBuffer.offset
        );
        break;
    case AccelerationStructureBuildMode::Update:
        encoder->refitAccelerationStructure(
            checked_cast<AccelerationStructureImpl*>(cmd.src)->m_accelerationStructure.get(),
            builder.descriptor.get(),
            checked_cast<AccelerationStructureImpl*>(cmd.dst)->m_accelerationStructure.get(),
            checked_cast<BufferImpl*>(cmd.scratchBuffer.buffer)->m_buffer.get(),
            cmd.scratchBuffer.offset
        );
        break;
    }

    // TODO handle queryDescs
}

void CommandRecorder::cmdCopyAccelerationStructure(const commands::CopyAccelerationStructure& cmd)
{
    MTL::AccelerationStructureCommandEncoder* encoder = getAccelerationStructureCommandEncoder();

    switch (cmd.mode)
    {
    case AccelerationStructureCopyMode::Clone:
        encoder->copyAccelerationStructure(
            checked_cast<AccelerationStructureImpl*>(cmd.src)->m_accelerationStructure.get(),
            checked_cast<AccelerationStructureImpl*>(cmd.dst)->m_accelerationStructure.get()
        );
        break;
    case AccelerationStructureCopyMode::Compact:
        encoder->copyAndCompactAccelerationStructure(
            checked_cast<AccelerationStructureImpl*>(cmd.src)->m_accelerationStructure.get(),
            checked_cast<AccelerationStructureImpl*>(cmd.dst)->m_accelerationStructure.get()
        );
        break;
    }
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

void CommandRecorder::cmdPushDebugGroup(const commands::PushDebugGroup& cmd)
{
    NS::SharedPtr<NS::String> string = MetalUtil::createString(cmd.name);
    m_commandBuffer->pushDebugGroup(string.get());
}

void CommandRecorder::cmdPopDebugGroup(const commands::PopDebugGroup& cmd)
{
    m_commandBuffer->popDebugGroup();
}

void CommandRecorder::cmdInsertDebugMarker(const commands::InsertDebugMarker& cmd)
{
    SLANG_UNUSED(cmd);
    // NS::SharedPtr<NS::String> string = MetalUtil::createString(cmd.name);
    // m_commandBuffer->insertDebugSignpost(string.get());
}

void CommandRecorder::cmdWriteTimestamp(const commands::WriteTimestamp& cmd)
{
    auto encoder = getBlitCommandEncoder();
    encoder->sampleCountersInBuffer(
        checked_cast<QueryPoolImpl*>(cmd.queryPool)->m_counterSampleBuffer.get(),
        cmd.queryIndex,
        true
    );
}

void CommandRecorder::cmdExecuteCallback(const commands::ExecuteCallback& cmd)
{
    cmd.callback(cmd.userData);
}

MTL::RenderCommandEncoder* CommandRecorder::getRenderCommandEncoder(MTL::RenderPassDescriptor* renderPassDesc)
{
    if (!m_renderCommandEncoder)
    {
        endCommandEncoder();
        m_renderCommandEncoder = NS::RetainPtr(m_commandBuffer->renderCommandEncoder(renderPassDesc));
    }
    return m_renderCommandEncoder.get();
}

MTL::ComputeCommandEncoder* CommandRecorder::getComputeCommandEncoder()
{
    if (!m_computeCommandEncoder)
    {
        endCommandEncoder();
        m_computeCommandEncoder = NS::RetainPtr(m_commandBuffer->computeCommandEncoder());
    }
    return m_computeCommandEncoder.get();
}

MTL::AccelerationStructureCommandEncoder* CommandRecorder::getAccelerationStructureCommandEncoder()
{
    if (!m_accelerationStructureCommandEncoder)
    {
        endCommandEncoder();
        m_accelerationStructureCommandEncoder = NS::RetainPtr(m_commandBuffer->accelerationStructureCommandEncoder());
    }
    return m_accelerationStructureCommandEncoder.get();
}

MTL::BlitCommandEncoder* CommandRecorder::getBlitCommandEncoder()
{
    if (!m_blitCommandEncoder)
    {
        endCommandEncoder();
        m_blitCommandEncoder = NS::RetainPtr(m_commandBuffer->blitCommandEncoder());
    }
    return m_blitCommandEncoder.get();
}

void CommandRecorder::endCommandEncoder()
{
    if (m_renderCommandEncoder)
    {
        m_renderCommandEncoder->endEncoding();
        m_renderCommandEncoder.reset();

        m_renderStateValid = false;
        m_renderState = {};
        m_renderPipeline = nullptr;
    }
    if (m_computeCommandEncoder)
    {
        m_computeCommandEncoder->endEncoding();
        m_computeCommandEncoder.reset();

        m_computeStateValid = false;
        m_computePipeline = nullptr;
    }
    if (m_accelerationStructureCommandEncoder)
    {
        m_accelerationStructureCommandEncoder->endEncoding();
        m_accelerationStructureCommandEncoder.reset();
    }
    if (m_blitCommandEncoder)
    {
        m_blitCommandEncoder->endEncoding();
        m_blitCommandEncoder.reset();
    }
    m_bindingData = nullptr;
}

// CommandQueueImpl

CommandQueueImpl::CommandQueueImpl(DeviceImpl* device, QueueType type)
    : CommandQueue(device, type)
{
}

CommandQueueImpl::~CommandQueueImpl() {}

void CommandQueueImpl::init(NS::SharedPtr<MTL::CommandQueue> commandQueue)
{
    m_commandQueue = commandQueue;
}

Result CommandQueueImpl::createCommandEncoder(ICommandEncoder** outEncoder)
{
    RefPtr<CommandEncoderImpl> encoder = new CommandEncoderImpl(m_device, this);
    SLANG_RETURN_ON_FAIL(encoder->init());
    returnComPtr(outEncoder, encoder);
    return SLANG_OK;
}

Result CommandQueueImpl::waitOnHost()
{
    // TODO implement
    return SLANG_OK;
}

Result CommandQueueImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLCommandQueue;
    outHandle->value = (uint64_t)m_commandQueue.get();
    return SLANG_OK;
}

Result CommandQueueImpl::submit(const SubmitDesc& desc)
{
    AUTORELEASEPOOL

    // If there are any wait fences, encode them to a new command buffer.
    // Metal ensures that command buffers are executed in the order they are committed.
    if (desc.waitFenceCount > 0)
    {
        MTL::CommandBuffer* commandBuffer = m_commandQueue->commandBuffer();
        for (uint32_t i = 0; i < desc.waitFenceCount; ++i)
        {
            FenceImpl* fence = checked_cast<FenceImpl*>(desc.waitFences[i]);
            commandBuffer->encodeWait(fence->m_event.get(), desc.waitFenceValues[i]);
        }
        commandBuffer->commit();
    }

    // Commit the command buffers.
    for (uint32_t i = 0; i < desc.commandBufferCount; ++i)
    {
        CommandBufferImpl* commandBuffer = checked_cast<CommandBufferImpl*>(desc.commandBuffers[i]);
        // Signal fences if this is the last command buffer.
        if (desc.signalFenceCount > 0 && i == desc.commandBufferCount - 1)
        {
            for (uint32_t j = 0; j < desc.signalFenceCount; ++j)
            {
                FenceImpl* fence = checked_cast<FenceImpl*>(desc.signalFences[j]);
                commandBuffer->m_commandBuffer->encodeSignalEvent(fence->m_event.get(), desc.signalFenceValues[j]);
            }
        }
        commandBuffer->m_commandBuffer->commit();
    }

    // If there are no command buffers to submit, but fences to signal, encode them to a new command buffer.
    if (desc.signalFenceCount > 0 && desc.commandBufferCount == 0)
    {
        MTL::CommandBuffer* commandBuffer = m_commandQueue->commandBuffer();
        for (uint32_t i = 0; i < desc.signalFenceCount; ++i)
        {
            FenceImpl* fence = checked_cast<FenceImpl*>(desc.signalFences[i]);
            commandBuffer->encodeSignalEvent(fence->m_event.get(), desc.signalFenceValues[i]);
        }
        commandBuffer->commit();
    }

    return SLANG_OK;
}

// CommandEncoderImpl

CommandEncoderImpl::CommandEncoderImpl(DeviceImpl* device, CommandQueueImpl* queue)
    : m_device(device)
    , m_queue(queue)
{
}

CommandEncoderImpl::~CommandEncoderImpl() {}

Result CommandEncoderImpl::init()
{
    m_commandBuffer = new CommandBufferImpl(m_device, m_queue);
    SLANG_RETURN_ON_FAIL(m_commandBuffer->init());
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
    CommandRecorder recorder(m_device);
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

CommandBufferImpl::CommandBufferImpl(DeviceImpl* device, CommandQueueImpl* queue)
    : m_device(device)
    , m_queue(queue)
{
}

CommandBufferImpl::~CommandBufferImpl() {}

Result CommandBufferImpl::init()
{
    m_commandBuffer = NS::RetainPtr(m_queue->m_commandQueue->commandBuffer());
    if (!m_commandBuffer)
    {
        return SLANG_FAIL;
    }
    return SLANG_OK;
}

Result CommandBufferImpl::reset()
{
    m_bindingCache.reset();
    return CommandBuffer::reset();
}

Result CommandBufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLCommandBuffer;
    outHandle->value = (uint64_t)m_commandBuffer.get();
    return SLANG_OK;
}

} // namespace rhi::metal
