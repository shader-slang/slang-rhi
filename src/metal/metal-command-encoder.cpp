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

void CommandEncoderImpl::init(DeviceImpl* device, CommandQueueImpl* queue)
{
    m_device = device;
    m_commandBuffer = new CommandBufferImpl();
    m_commandBuffer->m_commandBuffer = NS::RetainPtr(queue->m_commandQueue->commandBuffer());
    m_metalCommandBuffer = m_commandBuffer->m_commandBuffer;
}

Result CommandEncoderImpl::createRootShaderObject(ShaderProgram* program, ShaderObjectBase** outObject)
{
    RefPtr<RootShaderObjectImpl> object = new RootShaderObjectImpl();
    SLANG_RETURN_ON_FAIL(object->init(m_device, checked_cast<ShaderProgramImpl*>(program)->m_rootObjectLayout));
    returnRefPtr(outObject, object);
    return SLANG_OK;
}

void CommandEncoderImpl::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    auto encoder = getMetalBlitCommandEncoder();
    encoder->copyFromBuffer(
        checked_cast<BufferImpl*>(src)->m_buffer.get(),
        srcOffset,
        checked_cast<BufferImpl*>(dst)->m_buffer.get(),
        dstOffset,
        size
    );
}

void CommandEncoderImpl::uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data)
{
    SLANG_UNUSED(dst);
    SLANG_UNUSED(offset);
    SLANG_UNUSED(size);
    SLANG_UNUSED(data);
    SLANG_RHI_UNIMPLEMENTED("uploadBufferData");
}

void CommandEncoderImpl::copyTexture(
    ITexture* dst,
    SubresourceRange dstSubresource,
    Offset3D dstOffset,
    ITexture* src,
    SubresourceRange srcSubresource,
    Offset3D srcOffset,
    Extents extent
)
{
    auto encoder = getMetalBlitCommandEncoder();

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

void CommandEncoderImpl::uploadTextureData(
    ITexture* dst,
    SubresourceRange subresourceRange,
    Offset3D offset,
    Extents extent,
    SubresourceData* subresourceData,
    GfxCount subresourceDataCount
)
{
    SLANG_UNUSED(dst);
    SLANG_UNUSED(subresourceRange);
    SLANG_UNUSED(offset);
    SLANG_UNUSED(extent);
    SLANG_UNUSED(subresourceData);
    SLANG_UNUSED(subresourceDataCount);
    SLANG_RHI_UNIMPLEMENTED("uploadTextureData");
}

void CommandEncoderImpl::clearBuffer(IBuffer* buffer, const BufferRange* range)
{
    SLANG_UNUSED(buffer);
    SLANG_UNUSED(range);
    SLANG_RHI_UNIMPLEMENTED("clearBuffer");
}

void CommandEncoderImpl::clearTexture(
    ITexture* texture,
    const ClearValue& clearValue,
    const SubresourceRange* subresourceRange,
    bool clearDepth,
    bool clearStencil
)
{
    SLANG_UNUSED(texture);
    SLANG_UNUSED(clearValue);
    SLANG_UNUSED(subresourceRange);
    SLANG_UNUSED(clearDepth);
    SLANG_UNUSED(clearStencil);
    SLANG_RHI_UNIMPLEMENTED("clearBuffer");
}

void CommandEncoderImpl::resolveQuery(
    IQueryPool* queryPool,
    GfxIndex index,
    GfxCount count,
    IBuffer* buffer,
    Offset offset
)
{
    auto encoder = getMetalBlitCommandEncoder();
    encoder->resolveCounters(
        checked_cast<QueryPoolImpl*>(queryPool)->m_counterSampleBuffer.get(),
        NS::Range(index, count),
        checked_cast<BufferImpl*>(buffer)->m_buffer.get(),
        offset
    );
}

void CommandEncoderImpl::copyTextureToBuffer(
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

    auto encoder = getMetalBlitCommandEncoder();
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

void CommandEncoderImpl::beginRenderPass(const RenderPassDesc& desc)
{
    uint32_t width = 1;
    uint32_t height = 1;

    auto visitView = [&](TextureViewImpl* view)
    {
        const TextureDesc& textureDesc = view->m_texture->m_desc;
        const TextureViewDesc& viewDesc = view->m_desc;
        width = max(1u, uint32_t(textureDesc.size.width >> viewDesc.subresourceRange.mipLevel));
        height = max(1u, uint32_t(textureDesc.size.height >> viewDesc.subresourceRange.mipLevel));
        m_resources.push_back(view);
    };

    // Initialize render pass descriptor.
    NS::SharedPtr<MTL::RenderPassDescriptor> renderPassDesc =
        NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());

    // Setup color attachments.
    renderPassDesc->setRenderTargetArrayLength(desc.colorAttachmentCount);
    for (GfxIndex i = 0; i < desc.colorAttachmentCount; ++i)
    {
        const auto& attachment = desc.colorAttachments[i];
        TextureViewImpl* view = checked_cast<TextureViewImpl*>(attachment.view);
        if (!view)
            return;
        visitView(view);

        MTL::RenderPassColorAttachmentDescriptor* colorAttachment = renderPassDesc->colorAttachments()->object(i);
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
            return;
        visitView(view);

        MTL::PixelFormat pixelFormat = MetalUtil::translatePixelFormat(view->m_desc.format);
        if (MetalUtil::isDepthFormat(pixelFormat))
        {
            MTL::RenderPassDepthAttachmentDescriptor* depthAttachment = renderPassDesc->depthAttachment();
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
            MTL::RenderPassStencilAttachmentDescriptor* stencilAttachment = renderPassDesc->stencilAttachment();
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

    renderPassDesc->setRenderTargetWidth(width);
    renderPassDesc->setRenderTargetHeight(height);

    m_useDepthStencil = desc.depthStencilAttachment != nullptr;

    getMetalRenderCommandEncoder(renderPassDesc.get());
}

void CommandEncoderImpl::endRenderPass()
{
    endMetalCommandEncoder();
}

void CommandEncoderImpl::setRenderState(const RenderState& state)
{
    MTL::RenderCommandEncoder* encoder = m_metalRenderCommandEncoder.get();
    if (state.pipeline != m_renderState.pipeline)
    {
        m_renderPipeline = checked_cast<RenderPipelineImpl*>(state.pipeline);
        m_resources.push_back(m_renderPipeline);

        encoder->setRenderPipelineState(m_renderPipeline->m_pipelineState.get());
    }
    if (state.rootObject != m_renderState.rootObject)
    {
        m_rootObject = checked_cast<RootShaderObjectImpl*>(state.rootObject);
        m_resources.push_back(m_rootObject);

        RenderBindingContext bindingContext;
        bindingContext.init(m_device, encoder);
        m_rootObject->bindAsRoot(&bindingContext, m_renderPipeline->m_rootObjectLayout);
    }

    for (Index i = 0; i < state.vertexBufferCount; ++i)
    {
        BufferImpl* buffer = checked_cast<BufferImpl*>(state.vertexBuffers[i].buffer);
        encoder->setVertexBuffer(
            buffer->m_buffer.get(),
            state.vertexBuffers[i].offset,
            m_renderPipeline->m_vertexBufferOffset + i
        );
        m_resources.push_back(buffer);
    }

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
        m_resources.push_back(m_indexBuffer);
    }

    static_vector<MTL::Viewport, SLANG_COUNT_OF(RenderState::viewports)> viewports(state.viewportCount, {});
    for (Index i = 0; i < state.viewportCount; ++i)
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
    encoder->setViewports(viewports.data(), viewports.size());

    static_vector<MTL::ScissorRect, SLANG_COUNT_OF(RenderState::scissorRects)> scissorRects(state.scissorRectCount, {});
    for (Index i = 0; i < state.scissorRectCount; ++i)
    {
        const ScissorRect& src = state.scissorRects[i];
        MTL::ScissorRect& dst = scissorRects[i];
        dst.x = src.minX;
        dst.y = src.minY;
        dst.width = src.maxX - src.minX;
        dst.height = src.maxY - src.minY;
    }
    encoder->setScissorRects(scissorRects.data(), scissorRects.size());

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
    encoder->setStencilReferenceValue(state.stencilRef);

    m_renderState = state;

    m_computeState = {};
    m_computePipeline = nullptr;
    m_rayTracingState = {};
    m_rayTracingPipeline = nullptr;
}

void CommandEncoderImpl::draw(const DrawArguments& args)
{
    m_metalRenderCommandEncoder->drawPrimitives(
        m_renderPipeline->m_primitiveType,
        args.startVertexLocation,
        args.vertexCount,
        args.instanceCount,
        args.startInstanceLocation
    );
}

void CommandEncoderImpl::drawIndexed(const DrawArguments& args)
{
    m_metalRenderCommandEncoder->drawIndexedPrimitives(
        m_renderPipeline->m_primitiveType,
        args.vertexCount,
        m_indexType,
        m_indexBuffer->m_buffer.get(),
        m_indexBufferOffset + args.startIndexLocation * m_indexSize,

        args.instanceCount,
        args.startVertexLocation,
        args.startInstanceLocation
    );
}

void CommandEncoderImpl::drawIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    SLANG_UNUSED(maxDrawCount);
    SLANG_UNUSED(argBuffer);
    SLANG_UNUSED(argOffset);
    SLANG_UNUSED(countBuffer);
    SLANG_UNUSED(countOffset);
}

void CommandEncoderImpl::drawIndexedIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    SLANG_UNUSED(maxDrawCount);
    SLANG_UNUSED(argBuffer);
    SLANG_UNUSED(argOffset);
    SLANG_UNUSED(countBuffer);
    SLANG_UNUSED(countOffset);
}

void CommandEncoderImpl::drawMeshTasks(int x, int y, int z)
{
    SLANG_UNUSED(x);
    SLANG_UNUSED(y);
    SLANG_UNUSED(z);
}

void CommandEncoderImpl::setComputeState(const ComputeState& state)
{
    MTL::ComputeCommandEncoder* encoder = getMetalComputeCommandEncoder();
    if (state.pipeline != m_computeState.pipeline)
    {
        m_computePipeline = checked_cast<ComputePipelineImpl*>(state.pipeline);
        m_resources.push_back(m_computePipeline);

        encoder->setComputePipelineState(m_computePipeline->m_pipelineState.get());
    }
    if (state.rootObject != m_computeState.rootObject)
    {
        m_rootObject = checked_cast<RootShaderObjectImpl*>(state.rootObject);
        m_resources.push_back(m_rootObject);

        ComputeBindingContext bindingContext;
        bindingContext.init(m_device, encoder);
        m_rootObject->bindAsRoot(&bindingContext, m_computePipeline->m_rootObjectLayout);
    }

    m_computeState = state;

    m_renderState = {};
    m_renderPipeline = nullptr;
    m_rayTracingState = {};
    m_rayTracingPipeline = nullptr;
}

void CommandEncoderImpl::dispatchCompute(int x, int y, int z)
{
    m_metalComputeCommandEncoder->dispatchThreadgroups(MTL::Size(x, y, z), m_computePipeline->m_threadGroupSize);
}

void CommandEncoderImpl::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    SLANG_UNUSED(argBuffer);
    SLANG_UNUSED(offset);
    SLANG_RHI_UNIMPLEMENTED("dispatchComputeIndirect");
}

void CommandEncoderImpl::setRayTracingState(const RayTracingState& state)
{
    SLANG_UNUSED(state);
    SLANG_RHI_UNIMPLEMENTED("setRayTracingState");
}

void CommandEncoderImpl::dispatchRays(GfxIndex raygenShaderIndex, GfxCount width, GfxCount height, GfxCount depth) {}

void CommandEncoderImpl::buildAccelerationStructure(
    const AccelerationStructureBuildDesc& desc,
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    BufferWithOffset scratchBuffer,
    GfxCount propertyQueryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    MTL::AccelerationStructureCommandEncoder* encoder = getMetalAccelerationStructureCommandEncoder();

    AccelerationStructureDescBuilder builder;
    builder.build(desc, m_device->getAccelerationStructureArray(), m_device->m_debugCallback);

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

void CommandEncoderImpl::copyAccelerationStructure(
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    AccelerationStructureCopyMode mode
)
{
    MTL::AccelerationStructureCommandEncoder* encoder = getMetalAccelerationStructureCommandEncoder();

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

void CommandEncoderImpl::queryAccelerationStructureProperties(
    GfxCount accelerationStructureCount,
    IAccelerationStructure* const* accelerationStructures,
    GfxCount queryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    SLANG_UNUSED(accelerationStructureCount);
    SLANG_UNUSED(accelerationStructures);
    SLANG_UNUSED(queryCount);
    SLANG_UNUSED(queryDescs);
    SLANG_RHI_UNIMPLEMENTED("queryAccelerationStructureProperties");
}

void CommandEncoderImpl::serializeAccelerationStructure(BufferWithOffset dst, IAccelerationStructure* src)
{
    SLANG_UNUSED(dst);
    SLANG_UNUSED(src);
    SLANG_RHI_UNIMPLEMENTED("serializeAccelerationStructure");
}

void CommandEncoderImpl::deserializeAccelerationStructure(IAccelerationStructure* dst, BufferWithOffset src)
{
    SLANG_UNUSED(dst);
    SLANG_UNUSED(src);
    SLANG_RHI_UNIMPLEMENTED("deserializeAccelerationStructure");
}

void CommandEncoderImpl::setBufferState(IBuffer* buffer, ResourceState state)
{
    SLANG_UNUSED(buffer);
    SLANG_UNUSED(state);
    // We use metal's automatic resource state tracking.
}

void CommandEncoderImpl::setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state)
{
    SLANG_UNUSED(texture);
    SLANG_UNUSED(subresourceRange);
    SLANG_UNUSED(state);
    // We use metal's automatic resource state tracking.
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

void CommandEncoderImpl::writeTimestamp(IQueryPool* pool, GfxIndex index)
{
    auto encoder = getMetalBlitCommandEncoder();
    encoder->sampleCountersInBuffer(checked_cast<QueryPoolImpl*>(pool)->m_counterSampleBuffer.get(), index, true);
}

Result CommandEncoderImpl::finish(ICommandBuffer** outCommandBuffer)
{
    if (!m_commandBuffer)
    {
        return SLANG_FAIL;
    }
    endMetalCommandEncoder();
    m_commandBuffer->m_resources = std::move(m_resources);
    returnComPtr(outCommandBuffer, m_commandBuffer);
    m_commandBuffer = nullptr;
    return SLANG_OK;
}

Result CommandEncoderImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLCommandBuffer;
    outHandle->value = (uint64_t)m_metalCommandBuffer.get();
    return SLANG_OK;
}

MTL::RenderCommandEncoder* CommandEncoderImpl::getMetalRenderCommandEncoder(MTL::RenderPassDescriptor* renderPassDesc)
{
    if (!m_metalRenderCommandEncoder)
    {
        endMetalCommandEncoder();
        m_metalRenderCommandEncoder = NS::RetainPtr(m_metalCommandBuffer->renderCommandEncoder(renderPassDesc));
    }
    return m_metalRenderCommandEncoder.get();
}

MTL::ComputeCommandEncoder* CommandEncoderImpl::getMetalComputeCommandEncoder()
{
    if (!m_metalComputeCommandEncoder)
    {
        endMetalCommandEncoder();
        m_metalComputeCommandEncoder = NS::RetainPtr(m_metalCommandBuffer->computeCommandEncoder());
    }
    return m_metalComputeCommandEncoder.get();
}

MTL::AccelerationStructureCommandEncoder* CommandEncoderImpl::getMetalAccelerationStructureCommandEncoder()
{
    if (!m_metalAccelerationStructureCommandEncoder)
    {
        endMetalCommandEncoder();
        m_metalAccelerationStructureCommandEncoder =
            NS::RetainPtr(m_metalCommandBuffer->accelerationStructureCommandEncoder());
    }
    return m_metalAccelerationStructureCommandEncoder.get();
}

MTL::BlitCommandEncoder* CommandEncoderImpl::getMetalBlitCommandEncoder()
{
    if (!m_metalBlitCommandEncoder)
    {
        endMetalCommandEncoder();
        m_metalBlitCommandEncoder = NS::RetainPtr(m_metalCommandBuffer->blitCommandEncoder());
    }
    return m_metalBlitCommandEncoder.get();
}

void CommandEncoderImpl::endMetalCommandEncoder()
{
    if (m_metalRenderCommandEncoder)
    {
        m_metalRenderCommandEncoder->endEncoding();
        m_metalRenderCommandEncoder.reset();
    }
    if (m_metalComputeCommandEncoder)
    {
        m_metalComputeCommandEncoder->endEncoding();
        m_metalComputeCommandEncoder.reset();
    }
    if (m_metalAccelerationStructureCommandEncoder)
    {
        m_metalAccelerationStructureCommandEncoder->endEncoding();
        m_metalAccelerationStructureCommandEncoder.reset();
    }
    if (m_metalBlitCommandEncoder)
    {
        m_metalBlitCommandEncoder->endEncoding();
        m_metalBlitCommandEncoder.reset();
    }
}


} // namespace rhi::metal
