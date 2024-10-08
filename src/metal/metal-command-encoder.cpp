#include "metal-command-encoder.h"
#include "metal-buffer.h"
#include "metal-command-buffer.h"
#include "metal-helper-functions.h"
#include "metal-query.h"
#include "metal-shader-object.h"
#include "metal-shader-program.h"
#include "metal-shader-table.h"
#include "metal-texture.h"
#include "metal-texture-view.h"
#include "metal-util.h"
#include "metal-acceleration-structure.h"

namespace rhi::metal {

// PassEncoderImpl

void PassEncoderImpl::setBufferState(IBuffer* buffer, ResourceState state)
{
    // We use automatic hazard tracking for now, no need for barriers.
}

void PassEncoderImpl::setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state)
{
    // We use automatic hazard tracking for now, no need for barriers.
}

void PassEncoderImpl::beginDebugEvent(const char* name, float rgbColor[3])
{
    NS::SharedPtr<NS::String> string = MetalUtil::createString(name);
    m_commandBuffer->m_commandBuffer->pushDebugGroup(string.get());
}

void PassEncoderImpl::endDebugEvent()
{
    m_commandBuffer->m_commandBuffer->popDebugGroup();
}

void PassEncoderImpl::writeTimestamp(IQueryPool* queryPool, GfxIndex index)
{
    auto encoder = m_commandBuffer->getMetalBlitCommandEncoder();
    encoder->sampleCountersInBuffer(checked_cast<QueryPoolImpl*>(queryPool)->m_counterSampleBuffer.get(), index, true);
}


void PassEncoderImpl::init(CommandBufferImpl* commandBuffer)
{
    m_commandBuffer = commandBuffer;
    m_metalCommandBuffer = m_commandBuffer->m_commandBuffer.get();
}

void PassEncoderImpl::endEncodingImpl()
{
    m_commandBuffer->endMetalCommandEncoder();
}

Result PassEncoderImpl::setPipelineImpl(IPipeline* state, IShaderObject** outRootObject)
{
    m_currentPipeline = checked_cast<Pipeline*>(state);

    // m_commandBuffer->m_mutableRootShaderObject = nullptr;
    SLANG_RETURN_ON_FAIL(m_commandBuffer->m_rootObject.init(
        m_commandBuffer->m_device,
        checked_cast<ShaderProgramImpl*>(m_currentPipeline->m_program.get())->m_rootObjectLayout
    ));
    *outRootObject = &m_commandBuffer->m_rootObject;
    return SLANG_OK;
}

// ResourcePassEncoderImpl

void ResourcePassEncoderImpl::end()
{
    PassEncoderImpl::endEncodingImpl();
}

void ResourcePassEncoderImpl::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    auto encoder = m_commandBuffer->getMetalBlitCommandEncoder();
    encoder->copyFromBuffer(
        checked_cast<BufferImpl*>(src)->m_buffer.get(),
        srcOffset,
        checked_cast<BufferImpl*>(dst)->m_buffer.get(),
        dstOffset,
        size
    );
}

void ResourcePassEncoderImpl::copyTexture(
    ITexture* dst,
    SubresourceRange dstSubresource,
    Offset3D dstOffset,
    ITexture* src,
    SubresourceRange srcSubresource,
    Offset3D srcOffset,
    Extents extent
)
{
    auto encoder = m_commandBuffer->getMetalBlitCommandEncoder();

    if (dstSubresource.layerCount == 0 && dstSubresource.mipLevelCount == 0 && srcSubresource.layerCount == 0 &&
        srcSubresource.mipLevelCount == 0)
    {
        encoder->copyFromTexture(
            checked_cast<TextureImpl*>(src)->m_texture.get(),
            checked_cast<TextureImpl*>(dst)->m_texture.get()
        );
    }
    else
    {
        for (GfxIndex layer = 0; layer < dstSubresource.layerCount; layer++)
        {
            encoder->copyFromTexture(
                checked_cast<TextureImpl*>(src)->m_texture.get(),
                srcSubresource.baseArrayLayer + layer,
                srcSubresource.mipLevel,
                MTL::Origin(srcOffset.x, srcOffset.y, srcOffset.z),
                MTL::Size(extent.width, extent.height, extent.depth),
                checked_cast<TextureImpl*>(dst)->m_texture.get(),
                dstSubresource.baseArrayLayer + layer,
                dstSubresource.mipLevel,
                MTL::Origin(dstOffset.x, dstOffset.y, dstOffset.z)
            );
        }
    }
}

void ResourcePassEncoderImpl::copyTextureToBuffer(
    IBuffer* dst,
    Offset dstOffset,
    Size dstSize,
    Size dstRowStride,
    ITexture* src,
    SubresourceRange srcSubresource,
    Offset3D srcOffset,
    Extents extent
)
{
    SLANG_RHI_ASSERT(srcSubresource.mipLevelCount <= 1);

    auto encoder = m_commandBuffer->getMetalBlitCommandEncoder();
    encoder->copyFromTexture(
        checked_cast<TextureImpl*>(src)->m_texture.get(),
        srcSubresource.baseArrayLayer,
        srcSubresource.mipLevel,
        MTL::Origin(srcOffset.x, srcOffset.y, srcOffset.z),
        MTL::Size(extent.width, extent.height, extent.depth),
        checked_cast<BufferImpl*>(dst)->m_buffer.get(),
        dstOffset,
        dstRowStride,
        dstSize
    );
}

void ResourcePassEncoderImpl::uploadBufferData(IBuffer* buffer, Offset offset, Size size, void* data)
{
    SLANG_RHI_UNIMPLEMENTED("uploadBufferData");
}

void ResourcePassEncoderImpl::uploadTextureData(
    ITexture* dst,
    SubresourceRange subresourceRange,
    Offset3D offset,
    Extents extend,
    SubresourceData* subresourceData,
    GfxCount subresourceDataCount
)
{
    SLANG_RHI_UNIMPLEMENTED("uploadTextureData");
}

void ResourcePassEncoderImpl::clearBuffer(IBuffer* buffer, const BufferRange* range)
{
    SLANG_RHI_UNIMPLEMENTED("clearBuffer");
}

void ResourcePassEncoderImpl::clearTexture(
    ITexture* texture,
    const ClearValue& clearValue,
    const SubresourceRange* subresourceRange,
    bool clearDepth,
    bool clearStencil
)
{
    SLANG_RHI_UNIMPLEMENTED("clearTexture");
}

void ResourcePassEncoderImpl::resolveQuery(
    IQueryPool* queryPool,
    GfxIndex index,
    GfxCount count,
    IBuffer* buffer,
    Offset offset
)
{
    auto encoder = m_commandBuffer->getMetalBlitCommandEncoder();
    encoder->resolveCounters(
        checked_cast<QueryPoolImpl*>(queryPool)->m_counterSampleBuffer.get(),
        NS::Range(index, count),
        checked_cast<BufferImpl*>(buffer)->m_buffer.get(),
        offset
    );
}

// RenderPassEncoderImpl

Result RenderPassEncoderImpl::beginPass(const RenderPassDesc& desc)
{
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
    m_renderPassDesc = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());

    // Setup color attachments.
    m_renderTargetViews.resize(desc.colorAttachmentCount);
    m_renderPassDesc->setRenderTargetArrayLength(desc.colorAttachmentCount);
    for (GfxIndex i = 0; i < desc.colorAttachmentCount; ++i)
    {
        const auto& attachment = desc.colorAttachments[i];
        TextureViewImpl* view = checked_cast<TextureViewImpl*>(attachment.view);
        if (!view)
            return SLANG_FAIL;
        visitView(view);
        m_renderTargetViews[i] = view;

        MTL::RenderPassColorAttachmentDescriptor* colorAttachment = m_renderPassDesc->colorAttachments()->object(i);
        colorAttachment->setLoadAction(MetalUtil::translateLoadOp(attachment.loadOp));
        colorAttachment->setStoreAction(MetalUtil::translateStoreOp(attachment.storeOp));
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
            return SLANG_FAIL;
        visitView(view);
        m_depthStencilView = view;
        MTL::PixelFormat pixelFormat = MetalUtil::translatePixelFormat(view->m_desc.format);

        if (MetalUtil::isDepthFormat(pixelFormat))
        {
            MTL::RenderPassDepthAttachmentDescriptor* depthAttachment = m_renderPassDesc->depthAttachment();
            depthAttachment->setLoadAction(MetalUtil::translateLoadOp(attachment.depthLoadOp));
            depthAttachment->setStoreAction(MetalUtil::translateStoreOp(attachment.depthStoreOp));
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
            MTL::RenderPassStencilAttachmentDescriptor* stencilAttachment = m_renderPassDesc->stencilAttachment();
            stencilAttachment->setLoadAction(MetalUtil::translateLoadOp(attachment.stencilLoadOp));
            stencilAttachment->setStoreAction(MetalUtil::translateStoreOp(attachment.stencilStoreOp));
            if (attachment.stencilLoadOp == LoadOp::Clear)
            {
                stencilAttachment->setClearStencil(attachment.stencilClearValue);
            }
            stencilAttachment->setTexture(view->m_textureView.get());
            stencilAttachment->setLevel(view->m_desc.subresourceRange.mipLevel);
            stencilAttachment->setSlice(view->m_desc.subresourceRange.baseArrayLayer);
        }
    }

    m_renderPassDesc->setRenderTargetWidth(width);
    m_renderPassDesc->setRenderTargetHeight(height);

    return SLANG_OK;
}

void RenderPassEncoderImpl::end()
{
    auto encoder = m_commandBuffer->getMetalRenderCommandEncoder(m_renderPassDesc.get());

    m_renderTargetViews.clear();
    m_depthStencilView = nullptr;

    PassEncoderImpl::endEncodingImpl();
}

Result RenderPassEncoderImpl::bindPipeline(IPipeline* pipeline, IShaderObject** outRootObject)
{
    return setPipelineImpl(pipeline, outRootObject);
}

Result RenderPassEncoderImpl::bindPipelineWithRootObject(IPipeline* pipeline, IShaderObject* rootObject)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

void RenderPassEncoderImpl::setViewports(GfxCount count, const Viewport* viewports)
{
    m_viewports.resize(count);
    for (GfxIndex i = 0; i < count; ++i)
    {
        const auto& viewport = viewports[i];
        auto& mtlViewport = m_viewports[i];
        mtlViewport.originX = viewport.originX;
        mtlViewport.originY = viewport.originY;
        mtlViewport.width = viewport.extentX;
        mtlViewport.height = viewport.extentY;
        mtlViewport.znear = viewport.minZ;
        mtlViewport.zfar = viewport.maxZ;
    }
}

void RenderPassEncoderImpl::setScissorRects(GfxCount count, const ScissorRect* rects)
{
    m_scissorRects.resize(count);
    for (GfxIndex i = 0; i < count; ++i)
    {
        const auto& rect = rects[i];
        auto& mtlRect = m_scissorRects[i];
        mtlRect.x = rect.minX;
        mtlRect.y = rect.minY;
        mtlRect.width = rect.maxX - rect.minX;
        mtlRect.height = rect.maxY - rect.minY;
    }
}

void RenderPassEncoderImpl::setVertexBuffers(
    GfxIndex startSlot,
    GfxCount slotCount,
    IBuffer* const* buffers,
    const Offset* offsets
)
{
    Index count = max(m_vertexBuffers.size(), size_t(startSlot + slotCount));
    m_vertexBuffers.resize(count);
    m_vertexBufferOffsets.resize(count);

    for (Index i = 0; i < Index(slotCount); i++)
    {
        Index slotIndex = startSlot + i;
        m_vertexBuffers[slotIndex] = checked_cast<BufferImpl*>(buffers[i])->m_buffer.get();
        m_vertexBufferOffsets[slotIndex] = offsets[i];
    }
}

void RenderPassEncoderImpl::setIndexBuffer(IBuffer* buffer, IndexFormat indexFormat, Offset offset)
{
    m_indexBuffer = checked_cast<BufferImpl*>(buffer)->m_buffer.get();
    m_indexBufferOffset = offset;

    switch (indexFormat)
    {
    case IndexFormat::UInt16:
        m_indexBufferType = MTL::IndexTypeUInt16;
        break;
    case IndexFormat::UInt32:
        m_indexBufferType = MTL::IndexTypeUInt32;
        break;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported index format");
    }
}

void RenderPassEncoderImpl::setStencilReference(uint32_t referenceValue)
{
    m_stencilReferenceValue = referenceValue;
}

Result RenderPassEncoderImpl::setSamplePositions(
    GfxCount samplesPerPixel,
    GfxCount pixelCount,
    const SamplePosition* samplePositions
)
{
    return SLANG_E_NOT_AVAILABLE;
}

Result RenderPassEncoderImpl::prepareDraw(MTL::RenderCommandEncoder*& encoder)
{
    RootShaderObjectImpl* rootObjectImpl = &m_commandBuffer->m_rootObject;
    RefPtr<Pipeline> newPipeline;
    SLANG_RETURN_ON_FAIL(
        m_commandBuffer->m_device->maybeSpecializePipeline(m_currentPipeline, rootObjectImpl, newPipeline)
    );
    SLANG_RETURN_ON_FAIL(newPipeline->ensurePipelineCreated());
    m_currentPipeline = newPipeline;

    RenderPipelineImpl* renderPipeline = checked_cast<RenderPipelineImpl*>(m_currentPipeline->m_renderPipeline.get());
    m_primitiveType = renderPipeline->m_primitiveType;

    encoder = m_commandBuffer->getMetalRenderCommandEncoder(m_renderPassDesc.get());
    encoder->setRenderPipelineState(renderPipeline->m_pipelineState.get());

    RenderBindingContext bindingContext;
    bindingContext.init(m_commandBuffer->m_device, encoder);
    auto program = checked_cast<ShaderProgramImpl*>(m_currentPipeline->m_program.get());
    m_commandBuffer->m_rootObject.bindAsRoot(&bindingContext, program->m_rootObjectLayout);

    for (Index i = 0; i < m_vertexBuffers.size(); ++i)
    {
        encoder
            ->setVertexBuffer(m_vertexBuffers[i], m_vertexBufferOffsets[i], renderPipeline->m_vertexBufferOffset + i);
    }

    encoder->setViewports(m_viewports.data(), m_viewports.size());
    encoder->setScissorRects(m_scissorRects.data(), m_scissorRects.size());

    const RasterizerDesc& rasterizer = renderPipeline->m_rasterizerDesc;
    encoder->setFrontFacingWinding(MetalUtil::translateWinding(rasterizer.frontFace));
    encoder->setCullMode(MetalUtil::translateCullMode(rasterizer.cullMode));
    encoder->setDepthClipMode(
        rasterizer.depthClipEnable ? MTL::DepthClipModeClip : MTL::DepthClipModeClamp
    ); // TODO correct?
    encoder->setDepthBias(rasterizer.depthBias, rasterizer.slopeScaledDepthBias, rasterizer.depthBiasClamp);
    encoder->setTriangleFillMode(MetalUtil::translateTriangleFillMode(rasterizer.fillMode));
    // encoder->setBlendColor(); // not supported by rhi
    if (m_depthStencilView)
    {
        encoder->setDepthStencilState(renderPipeline->m_depthStencilState.get());
    }
    encoder->setStencilReferenceValue(m_stencilReferenceValue);

    return SLANG_OK;
}

Result RenderPassEncoderImpl::draw(GfxCount vertexCount, GfxIndex startVertex)
{
    MTL::RenderCommandEncoder* encoder;
    SLANG_RETURN_ON_FAIL(prepareDraw(encoder));
    encoder->drawPrimitives(m_primitiveType, startVertex, vertexCount);
    return SLANG_OK;
}

Result RenderPassEncoderImpl::drawIndexed(GfxCount indexCount, GfxIndex startIndex, GfxIndex baseVertex)
{
    MTL::RenderCommandEncoder* encoder;
    SLANG_RETURN_ON_FAIL(prepareDraw(encoder));
    // TODO baseVertex is not supported by Metal
    encoder->drawIndexedPrimitives(m_primitiveType, indexCount, m_indexBufferType, m_indexBuffer, m_indexBufferOffset);
    return SLANG_OK;
}

Result RenderPassEncoderImpl::drawIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

Result RenderPassEncoderImpl::drawIndexedIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

Result RenderPassEncoderImpl::drawInstanced(
    GfxCount vertexCount,
    GfxCount instanceCount,
    GfxIndex startVertex,
    GfxIndex startInstanceLocation
)
{
    MTL::RenderCommandEncoder* encoder;
    SLANG_RETURN_ON_FAIL(prepareDraw(encoder));
    encoder->drawPrimitives(m_primitiveType, startVertex, vertexCount, instanceCount, startInstanceLocation);
    return SLANG_OK;
}

Result RenderPassEncoderImpl::drawIndexedInstanced(
    GfxCount indexCount,
    GfxCount instanceCount,
    GfxIndex startIndexLocation,
    GfxIndex baseVertexLocation,
    GfxIndex startInstanceLocation
)
{
    MTL::RenderCommandEncoder* encoder;
    SLANG_RETURN_ON_FAIL(prepareDraw(encoder));
    encoder->drawIndexedPrimitives(
        m_primitiveType,
        indexCount,
        m_indexBufferType,
        m_indexBuffer,
        startIndexLocation,
        instanceCount,
        baseVertexLocation,
        startIndexLocation
    );
    return SLANG_OK;
}

Result RenderPassEncoderImpl::drawMeshTasks(int x, int y, int z)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

// ComputePassEncoderImpl

void ComputePassEncoderImpl::end()
{
    PassEncoderImpl::endEncodingImpl();
}

Result ComputePassEncoderImpl::bindPipeline(IPipeline* pipeline, IShaderObject** outRootObject)
{
    return setPipelineImpl(pipeline, outRootObject);
}

Result ComputePassEncoderImpl::bindPipelineWithRootObject(IPipeline* pipeline, IShaderObject* rootObject)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

Result ComputePassEncoderImpl::dispatchCompute(int x, int y, int z)
{
    RootShaderObjectImpl* rootObjectImpl = &m_commandBuffer->m_rootObject;
    RefPtr<Pipeline> newPipeline;
    SLANG_RETURN_ON_FAIL(
        m_commandBuffer->m_device->maybeSpecializePipeline(m_currentPipeline, rootObjectImpl, newPipeline)
    );
    SLANG_RETURN_ON_FAIL(newPipeline->ensurePipelineCreated());
    m_currentPipeline = newPipeline;

    MTL::ComputeCommandEncoder* encoder = m_commandBuffer->getMetalComputeCommandEncoder();

    ComputeBindingContext bindingContext;
    bindingContext.init(m_commandBuffer->m_device, encoder);
    auto program = checked_cast<ShaderProgramImpl*>(m_currentPipeline->m_program.get());
    m_commandBuffer->m_rootObject.bindAsRoot(&bindingContext, program->m_rootObjectLayout);

    ComputePipelineImpl* computePipeline =
        checked_cast<ComputePipelineImpl*>(m_currentPipeline->m_computePipeline.get());

    encoder->setComputePipelineState(computePipeline->m_pipelineState.get());
    encoder->dispatchThreadgroups(MTL::Size(x, y, z), computePipeline->m_threadGroupSize);

    return SLANG_OK;
}

Result ComputePassEncoderImpl::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    SLANG_RHI_UNIMPLEMENTED("dispatchComputeIndirect");
}

// RayTracingPassEncoderImpl

void RayTracingPassEncoderImpl::end()
{
    PassEncoderImpl::endEncodingImpl();
}

void RayTracingPassEncoderImpl::buildAccelerationStructure(
    const AccelerationStructureBuildDesc& desc,
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    BufferWithOffset scratchBuffer,
    GfxCount propertyQueryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    MTL::AccelerationStructureCommandEncoder* encoder = m_commandBuffer->getMetalAccelerationStructureCommandEncoder();

    AccelerationStructureDescBuilder builder;
    builder.build(
        desc,
        m_commandBuffer->m_device->getAccelerationStructureArray(),
        m_commandBuffer->m_device->m_debugCallback
    );

    switch (desc.mode)
    {
    case AccelerationStructureBuildMode::Build:
        encoder->buildAccelerationStructure(
            checked_cast<AccelerationStructureImpl*>(dst)->m_accelerationStructure.get(),
            builder.descriptor.get(),
            checked_cast<BufferImpl*>(scratchBuffer.buffer)->m_buffer.get(),
            scratchBuffer.offset
        );
        break;
    case AccelerationStructureBuildMode::Update:
        encoder->refitAccelerationStructure(
            checked_cast<AccelerationStructureImpl*>(src)->m_accelerationStructure.get(),
            builder.descriptor.get(),
            checked_cast<AccelerationStructureImpl*>(dst)->m_accelerationStructure.get(),
            checked_cast<BufferImpl*>(scratchBuffer.buffer)->m_buffer.get(),
            scratchBuffer.offset
        );
        break;
    }

    // TODO handle queryDescs
}

void RayTracingPassEncoderImpl::copyAccelerationStructure(
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    AccelerationStructureCopyMode mode
)
{
    MTL::AccelerationStructureCommandEncoder* encoder = m_commandBuffer->getMetalAccelerationStructureCommandEncoder();

    switch (mode)
    {
    case AccelerationStructureCopyMode::Clone:
        encoder->copyAccelerationStructure(
            checked_cast<AccelerationStructureImpl*>(src)->m_accelerationStructure.get(),
            checked_cast<AccelerationStructureImpl*>(dst)->m_accelerationStructure.get()
        );
        break;
    case AccelerationStructureCopyMode::Compact:
        encoder->copyAndCompactAccelerationStructure(
            checked_cast<AccelerationStructureImpl*>(src)->m_accelerationStructure.get(),
            checked_cast<AccelerationStructureImpl*>(dst)->m_accelerationStructure.get()
        );
        break;
    }
}

void RayTracingPassEncoderImpl::queryAccelerationStructureProperties(
    GfxCount accelerationStructureCount,
    IAccelerationStructure* const* accelerationStructures,
    GfxCount queryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
}

void RayTracingPassEncoderImpl::serializeAccelerationStructure(BufferWithOffset dst, IAccelerationStructure* src) {}

void RayTracingPassEncoderImpl::deserializeAccelerationStructure(IAccelerationStructure* dst, BufferWithOffset src) {}

Result RayTracingPassEncoderImpl::bindPipeline(IPipeline* pipeline, IShaderObject** outRootObject)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

Result RayTracingPassEncoderImpl::bindPipelineWithRootObject(IPipeline* pipeline, IShaderObject* rootObject)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

Result RayTracingPassEncoderImpl::dispatchRays(
    GfxIndex raygenShaderIndex,
    IShaderTable* shaderTable,
    GfxCount width,
    GfxCount height,
    GfxCount depth
)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

} // namespace rhi::metal
