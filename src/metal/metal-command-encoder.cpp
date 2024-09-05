#include "metal-command-encoder.h"
#include "metal-buffer.h"
#include "metal-command-buffer.h"
#include "metal-helper-functions.h"
#include "metal-query.h"
#include "metal-render-pass.h"
#include "metal-resource-views.h"
#include "metal-shader-object.h"
#include "metal-shader-program.h"
#include "metal-shader-table.h"
#include "metal-texture.h"
#include "metal-util.h"

namespace rhi::metal {

// CommandEncoderImpl

void CommandEncoderImpl::textureBarrier(GfxCount count, ITexture* const* textures, ResourceState src, ResourceState dst)
{
    // We use automatic hazard tracking for now, no need for barriers.
}

void CommandEncoderImpl::textureSubresourceBarrier(
    ITexture* texture,
    SubresourceRange subresourceRange,
    ResourceState src,
    ResourceState dst
)
{
    // We use automatic hazard tracking for now, no need for barriers.
}

void CommandEncoderImpl::bufferBarrier(GfxCount count, IBuffer* const* buffers, ResourceState src, ResourceState dst)
{
    // We use automatic hazard tracking for now, no need for barriers.
}

void CommandEncoderImpl::beginDebugEvent(const char* name, float rgbColor[3])
{
    NS::SharedPtr<NS::String> string = MetalUtil::createString(name);
    m_commandBuffer->m_commandBuffer->pushDebugGroup(string.get());
}

void CommandEncoderImpl::endDebugEvent()
{
    m_commandBuffer->m_commandBuffer->popDebugGroup();
}

void CommandEncoderImpl::writeTimestamp(IQueryPool* queryPool, GfxIndex index)
{
    auto encoder = m_commandBuffer->getMetalBlitCommandEncoder();
    encoder->sampleCountersInBuffer(static_cast<QueryPoolImpl*>(queryPool)->m_counterSampleBuffer.get(), index, true);
}


void CommandEncoderImpl::init(CommandBufferImpl* commandBuffer)
{
    m_commandBuffer = commandBuffer;
    m_metalCommandBuffer = m_commandBuffer->m_commandBuffer.get();
}

void CommandEncoderImpl::endEncodingImpl()
{
    m_commandBuffer->endMetalCommandEncoder();
}

Result CommandEncoderImpl::setPipelineImpl(IPipeline* state, IShaderObject** outRootObject)
{
    m_currentPipeline = static_cast<PipelineImpl*>(state);
    // m_commandBuffer->m_mutableRootShaderObject = nullptr;
    SLANG_RETURN_ON_FAIL(m_commandBuffer->m_rootObject.init(
        m_commandBuffer->m_device,
        m_currentPipeline->getProgram<ShaderProgramImpl>()->m_rootObjectLayout
    ));
    *outRootObject = &m_commandBuffer->m_rootObject;
    return SLANG_OK;
}

// ResourceCommandEncoderImpl

void ResourceCommandEncoderImpl::endEncoding()
{
    CommandEncoderImpl::endEncodingImpl();
}

void ResourceCommandEncoderImpl::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    auto encoder = m_commandBuffer->getMetalBlitCommandEncoder();
    encoder->copyFromBuffer(
        static_cast<BufferImpl*>(src)->m_buffer.get(),
        srcOffset,
        static_cast<BufferImpl*>(dst)->m_buffer.get(),
        dstOffset,
        size
    );
}

void ResourceCommandEncoderImpl::copyTexture(
    ITexture* dst,
    ResourceState dstState,
    SubresourceRange dstSubresource,
    Offset3D dstOffset,
    ITexture* src,
    ResourceState srcState,
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
            static_cast<TextureImpl*>(src)->m_texture.get(),
            static_cast<TextureImpl*>(dst)->m_texture.get()
        );
    }
    else
    {
        for (GfxIndex layer = 0; layer < dstSubresource.layerCount; layer++)
        {
            encoder->copyFromTexture(
                static_cast<TextureImpl*>(src)->m_texture.get(),
                srcSubresource.baseArrayLayer + layer,
                srcSubresource.mipLevel,
                MTL::Origin(srcOffset.x, srcOffset.y, srcOffset.z),
                MTL::Size(extent.width, extent.height, extent.depth),
                static_cast<TextureImpl*>(dst)->m_texture.get(),
                dstSubresource.baseArrayLayer + layer,
                dstSubresource.mipLevel,
                MTL::Origin(dstOffset.x, dstOffset.y, dstOffset.z)
            );
        }
    }
}

void ResourceCommandEncoderImpl::copyTextureToBuffer(
    IBuffer* dst,
    Offset dstOffset,
    Size dstSize,
    Size dstRowStride,
    ITexture* src,
    ResourceState srcState,
    SubresourceRange srcSubresource,
    Offset3D srcOffset,
    Extents extent
)
{
    SLANG_RHI_ASSERT(srcSubresource.mipLevelCount <= 1);

    auto encoder = m_commandBuffer->getMetalBlitCommandEncoder();
    encoder->copyFromTexture(
        static_cast<TextureImpl*>(src)->m_texture.get(),
        srcSubresource.baseArrayLayer,
        srcSubresource.mipLevel,
        MTL::Origin(srcOffset.x, srcOffset.y, srcOffset.z),
        MTL::Size(extent.width, extent.height, extent.depth),
        static_cast<BufferImpl*>(dst)->m_buffer.get(),
        dstOffset,
        dstRowStride,
        dstSize
    );
}

void ResourceCommandEncoderImpl::uploadBufferData(IBuffer* buffer, Offset offset, Size size, void* data)
{
    SLANG_RHI_UNIMPLEMENTED("uploadBufferData");
}

void ResourceCommandEncoderImpl::uploadTextureData(
    ITexture* dst,
    SubresourceRange subResourceRange,
    Offset3D offset,
    Extents extend,
    SubresourceData* subResourceData,
    GfxCount subResourceDataCount
)
{
    SLANG_RHI_UNIMPLEMENTED("uploadTextureData");
}

void ResourceCommandEncoderImpl::clearResourceView(
    IResourceView* view,
    ClearValue* clearValue,
    ClearResourceViewFlags::Enum flags
)
{
    SLANG_RHI_UNIMPLEMENTED("clearResourceView");
}

void ResourceCommandEncoderImpl::resolveResource(
    ITexture* source,
    ResourceState sourceState,
    SubresourceRange sourceRange,
    ITexture* dest,
    ResourceState destState,
    SubresourceRange destRange
)
{
    SLANG_RHI_UNIMPLEMENTED("resolveResource");
}

void ResourceCommandEncoderImpl::resolveQuery(
    IQueryPool* queryPool,
    GfxIndex index,
    GfxCount count,
    IBuffer* buffer,
    Offset offset
)
{
    auto encoder = m_commandBuffer->getMetalBlitCommandEncoder();
    encoder->resolveCounters(
        static_cast<QueryPoolImpl*>(queryPool)->m_counterSampleBuffer.get(),
        NS::Range(index, count),
        static_cast<BufferImpl*>(buffer)->m_buffer.get(),
        offset
    );
}

// RenderCommandEncoderImpl

void RenderCommandEncoderImpl::beginPass(IRenderPassLayout* renderPass, IFramebuffer* framebuffer)
{
    m_renderPassLayout = static_cast<RenderPassLayoutImpl*>(renderPass);
    m_framebuffer = static_cast<FramebufferImpl*>(framebuffer);
    if (!m_framebuffer)
    {
        // TODO use empty framebuffer
        return;
    }

    // Create a copy of the render pass descriptor and fill in remaining information.
    m_renderPassDesc = NS::TransferPtr(m_renderPassLayout->m_renderPassDesc->copy());

    m_renderPassDesc->setRenderTargetWidth(m_framebuffer->m_width);
    m_renderPassDesc->setRenderTargetHeight(m_framebuffer->m_height);

    for (Index i = 0; i < m_framebuffer->m_renderTargetViews.size(); ++i)
    {
        TextureViewImpl* renderTargetView = m_framebuffer->m_renderTargetViews[i];
        MTL::RenderPassColorAttachmentDescriptor* colorAttachment = m_renderPassDesc->colorAttachments()->object(i);
        colorAttachment->setTexture(renderTargetView->m_textureView.get());
        colorAttachment->setLevel(renderTargetView->m_desc.subresourceRange.mipLevel);
        colorAttachment->setSlice(renderTargetView->m_desc.subresourceRange.baseArrayLayer);
    }

    if (m_framebuffer->m_depthStencilView)
    {
        TextureViewImpl* depthStencilView = m_framebuffer->m_depthStencilView.get();
        MTL::PixelFormat pixelFormat = MetalUtil::translatePixelFormat(depthStencilView->m_desc.format);
        if (MetalUtil::isDepthFormat(pixelFormat))
        {
            MTL::RenderPassDepthAttachmentDescriptor* depthAttachment = m_renderPassDesc->depthAttachment();
            depthAttachment->setTexture(depthStencilView->m_textureView.get());
            depthAttachment->setLevel(depthStencilView->m_desc.subresourceRange.mipLevel);
            depthAttachment->setSlice(depthStencilView->m_desc.subresourceRange.baseArrayLayer);
        }
        if (MetalUtil::isStencilFormat(pixelFormat))
        {
            MTL::RenderPassStencilAttachmentDescriptor* stencilAttachment = m_renderPassDesc->stencilAttachment();
            stencilAttachment->setTexture(depthStencilView->m_textureView.get());
            stencilAttachment->setLevel(depthStencilView->m_desc.subresourceRange.mipLevel);
            stencilAttachment->setSlice(depthStencilView->m_desc.subresourceRange.baseArrayLayer);
        }
    }
}

void RenderCommandEncoderImpl::endEncoding()
{
    CommandEncoderImpl::endEncodingImpl();
}

Result RenderCommandEncoderImpl::bindPipeline(IPipeline* pipeline, IShaderObject** outRootObject)
{
    return setPipelineImpl(pipeline, outRootObject);
}

Result RenderCommandEncoderImpl::bindPipelineWithRootObject(IPipeline* pipeline, IShaderObject* rootObject)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

void RenderCommandEncoderImpl::setViewports(GfxCount count, const Viewport* viewports)
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

void RenderCommandEncoderImpl::setScissorRects(GfxCount count, const ScissorRect* rects)
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

void RenderCommandEncoderImpl::setPrimitiveTopology(PrimitiveTopology topology)
{
    m_primitiveType = MetalUtil::translatePrimitiveType(topology);
}

void RenderCommandEncoderImpl::setVertexBuffers(
    GfxIndex startSlot,
    GfxCount slotCount,
    IBuffer* const* buffers,
    const Offset* offsets
)
{
    Index count = std::max(m_vertexBuffers.size(), size_t(startSlot + slotCount));
    m_vertexBuffers.resize(count);
    m_vertexBufferOffsets.resize(count);

    for (Index i = 0; i < Index(slotCount); i++)
    {
        Index slotIndex = startSlot + i;
        m_vertexBuffers[slotIndex] = static_cast<BufferImpl*>(buffers[i])->m_buffer.get();
        m_vertexBufferOffsets[slotIndex] = offsets[i];
    }
}

void RenderCommandEncoderImpl::setIndexBuffer(IBuffer* buffer, Format indexFormat, Offset offset)
{
    m_indexBuffer = static_cast<BufferImpl*>(buffer)->m_buffer.get();
    m_indexBufferOffset = offset;

    switch (indexFormat)
    {
    case Format::R16_UINT:
        m_indexBufferType = MTL::IndexTypeUInt16;
        break;
    case Format::R32_UINT:
        m_indexBufferType = MTL::IndexTypeUInt32;
        break;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported index format");
    }
}

void RenderCommandEncoderImpl::setStencilReference(uint32_t referenceValue)
{
    m_stencilReferenceValue = referenceValue;
}

Result RenderCommandEncoderImpl::setSamplePositions(
    GfxCount samplesPerPixel,
    GfxCount pixelCount,
    const SamplePosition* samplePositions
)
{
    return SLANG_E_NOT_AVAILABLE;
}

Result RenderCommandEncoderImpl::prepareDraw(MTL::RenderCommandEncoder*& encoder)
{
    auto pipeline = static_cast<PipelineImpl*>(m_currentPipeline.Ptr());
    pipeline->ensureAPIPipelineCreated();

    encoder = m_commandBuffer->getMetalRenderCommandEncoder(m_renderPassDesc.get());
    encoder->setRenderPipelineState(pipeline->m_renderPipelineState.get());

    RenderBindingContext bindingContext;
    bindingContext.init(m_commandBuffer->m_device, encoder);
    auto program = static_cast<ShaderProgramImpl*>(m_currentPipeline->m_program.get());
    m_commandBuffer->m_rootObject.bindAsRoot(&bindingContext, program->m_rootObjectLayout);

    for (Index i = 0; i < m_vertexBuffers.size(); ++i)
    {
        encoder->setVertexBuffer(
            m_vertexBuffers[i],
            m_vertexBufferOffsets[i],
            m_currentPipeline->m_vertexBufferOffset + i
        );
    }

    encoder->setViewports(m_viewports.data(), m_viewports.size());
    encoder->setScissorRects(m_scissorRects.data(), m_scissorRects.size());

    const RasterizerDesc& rasterDesc = pipeline->desc.graphics.rasterizer;
    const DepthStencilDesc& depthStencilDesc = pipeline->desc.graphics.depthStencil;
    encoder->setFrontFacingWinding(MetalUtil::translateWinding(rasterDesc.frontFace));
    encoder->setCullMode(MetalUtil::translateCullMode(rasterDesc.cullMode));
    encoder->setDepthClipMode(
        rasterDesc.depthClipEnable ? MTL::DepthClipModeClip : MTL::DepthClipModeClamp
    ); // TODO correct?
    encoder->setDepthBias(rasterDesc.depthBias, rasterDesc.slopeScaledDepthBias, rasterDesc.depthBiasClamp);
    encoder->setTriangleFillMode(MetalUtil::translateTriangleFillMode(rasterDesc.fillMode));
    // encoder->setBlendColor(); // not supported by rhi
    if (m_framebuffer->m_depthStencilView)
    {
        encoder->setDepthStencilState(pipeline->m_depthStencilState.get());
    }
    encoder->setStencilReferenceValue(m_stencilReferenceValue);

    return SLANG_OK;
}

Result RenderCommandEncoderImpl::draw(GfxCount vertexCount, GfxIndex startVertex)
{
    MTL::RenderCommandEncoder* encoder;
    SLANG_RETURN_ON_FAIL(prepareDraw(encoder));
    encoder->drawPrimitives(m_primitiveType, startVertex, vertexCount);
    return SLANG_OK;
}

Result RenderCommandEncoderImpl::drawIndexed(GfxCount indexCount, GfxIndex startIndex, GfxIndex baseVertex)
{
    MTL::RenderCommandEncoder* encoder;
    SLANG_RETURN_ON_FAIL(prepareDraw(encoder));
    // TODO baseVertex is not supported by Metal
    encoder->drawIndexedPrimitives(m_primitiveType, indexCount, m_indexBufferType, m_indexBuffer, m_indexBufferOffset);
    return SLANG_OK;
}

Result RenderCommandEncoderImpl::drawIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

Result RenderCommandEncoderImpl::drawIndexedIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

Result RenderCommandEncoderImpl::drawInstanced(
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

Result RenderCommandEncoderImpl::drawIndexedInstanced(
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

Result RenderCommandEncoderImpl::drawMeshTasks(int x, int y, int z)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

// ComputeCommandEncoderImpl

void ComputeCommandEncoderImpl::endEncoding()
{
    CommandEncoderImpl::endEncodingImpl();
}

Result ComputeCommandEncoderImpl::bindPipeline(IPipeline* pipeline, IShaderObject** outRootObject)
{
    return setPipelineImpl(pipeline, outRootObject);
}

Result ComputeCommandEncoderImpl::bindPipelineWithRootObject(IPipeline* pipeline, IShaderObject* rootObject)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

Result ComputeCommandEncoderImpl::dispatchCompute(int x, int y, int z)
{
    MTL::ComputeCommandEncoder* encoder = m_commandBuffer->getMetalComputeCommandEncoder();

    ComputeBindingContext bindingContext;
    bindingContext.init(m_commandBuffer->m_device, encoder);
    auto program = static_cast<ShaderProgramImpl*>(m_currentPipeline->m_program.get());
    m_commandBuffer->m_rootObject.bindAsRoot(&bindingContext, program->m_rootObjectLayout);

    auto pipeline = static_cast<PipelineImpl*>(m_currentPipeline.Ptr());
    RootShaderObjectImpl* rootObjectImpl = &m_commandBuffer->m_rootObject;
    RefPtr<PipelineBase> newPipeline;
    SLANG_RETURN_ON_FAIL(
        m_commandBuffer->m_device->maybeSpecializePipeline(m_currentPipeline, rootObjectImpl, newPipeline)
    );
    PipelineImpl* newPipelineImpl = static_cast<PipelineImpl*>(newPipeline.Ptr());

    SLANG_RETURN_ON_FAIL(newPipelineImpl->ensureAPIPipelineCreated());
    m_currentPipeline = newPipelineImpl;

    m_currentPipeline->ensureAPIPipelineCreated();
    encoder->setComputePipelineState(m_currentPipeline->m_computePipelineState.get());

    encoder->dispatchThreadgroups(MTL::Size(x, y, z), m_currentPipeline->m_threadGroupSize);

    return SLANG_OK;
}

Result ComputeCommandEncoderImpl::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    SLANG_RHI_UNIMPLEMENTED("dispatchComputeIndirect");
}

// RayTracingCommandEncoderImpl

void RayTracingCommandEncoderImpl::endEncoding()
{
    CommandEncoderImpl::endEncodingImpl();
}

void RayTracingCommandEncoderImpl::buildAccelerationStructure(
    const IAccelerationStructure::BuildDesc& desc,
    GfxCount propertyQueryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
}

void RayTracingCommandEncoderImpl::copyAccelerationStructure(
    IAccelerationStructure* dest,
    IAccelerationStructure* src,
    AccelerationStructureCopyMode mode
)
{
}

void RayTracingCommandEncoderImpl::queryAccelerationStructureProperties(
    GfxCount accelerationStructureCount,
    IAccelerationStructure* const* accelerationStructures,
    GfxCount queryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
}

void RayTracingCommandEncoderImpl::serializeAccelerationStructure(DeviceAddress dest, IAccelerationStructure* source) {}

void RayTracingCommandEncoderImpl::deserializeAccelerationStructure(IAccelerationStructure* dest, DeviceAddress source)
{
}

Result RayTracingCommandEncoderImpl::bindPipeline(IPipeline* pipeline, IShaderObject** outRootObject)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

Result RayTracingCommandEncoderImpl::bindPipelineWithRootObject(IPipeline* pipeline, IShaderObject* rootObject)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

Result RayTracingCommandEncoderImpl::dispatchRays(
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
