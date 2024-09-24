#include "wgpu-command-encoder.h"
#include "wgpu-command-buffer.h"
#include "wgpu-pipeline.h"
#include "wgpu-shader-program.h"
#include "wgpu-buffer.h"
#include "wgpu-texture.h"
#include "wgpu-device.h"
#include "wgpu-transient-resource-heap.h"
#include "wgpu-util.h"

namespace rhi::wgpu {

void* CommandEncoderImpl::getInterface(SlangUUID const& uuid)
{
    if (uuid == GUID::IID_ICommandEncoder || uuid == ISlangUnknown::getTypeGuid())
        return this;
    return nullptr;
}

Result CommandEncoderImpl::queryInterface(SlangUUID const& uuid, void** outObject)
{
    if (auto ptr = getInterface(uuid))
    {
        *outObject = ptr;
        return SLANG_OK;
    }
    return SLANG_E_NO_INTERFACE;
}

uint32_t CommandEncoderImpl::addRef()
{
    return 1;
}

uint32_t CommandEncoderImpl::release()
{
    return 1;
}

CommandEncoderImpl::~CommandEncoderImpl() {}

void CommandEncoderImpl::init(CommandBufferImpl* commandBuffer)
{
    m_device = commandBuffer->m_device;
    m_commandBuffer = commandBuffer;
    m_commandEncoder = commandBuffer->m_commandEncoder;
}

Result CommandEncoderImpl::bindPipelineImpl(RootBindingContext& context)
{
    // Get specialized pipeline state and bind it.
    RootShaderObjectImpl* rootObjectImpl = m_commandBuffer->m_mutableRootShaderObject
                                               ? m_commandBuffer->m_mutableRootShaderObject.Ptr()
                                               : &m_commandBuffer->m_rootObject;
    RefPtr<Pipeline> newPipeline;
    SLANG_RETURN_ON_FAIL(m_device->maybeSpecializePipeline(m_currentPipeline, rootObjectImpl, newPipeline));
    PipelineImpl* newPipelineImpl = static_cast<PipelineImpl*>(newPipeline.Ptr());

    SLANG_RETURN_ON_FAIL(newPipelineImpl->ensureAPIPipelineCreated());
    m_currentPipeline = newPipelineImpl;

    // Obtain specialized root layout.
    auto specializedLayout = rootObjectImpl->getSpecializedLayout();
    if (!specializedLayout)
        return SLANG_FAIL;

    // We will set up the context required when binding shader objects
    // to the pipeline. Note that this is mostly just being packaged
    // together to minimize the number of parameters that have to
    // be dealt with in the complex recursive call chains.
    //
    context.bindGroupLayouts = specializedLayout->m_bindGroupLayouts;
    context.device = m_commandBuffer->m_device;

    // We kick off recursive binding of shader objects to the pipeline (plus
    // the state in `context`).
    //
    // Note: this logic will directly write any push-constant ranges needed,
    // and will also fill in any descriptor sets. Currently it does not
    // *bind* the descriptor sets it fills in.
    //
    // TODO: It could probably bind the descriptor sets as well.
    //
    rootObjectImpl->bindAsRoot(this, context, specializedLayout);

    return SLANG_OK;
}

void CommandEncoderImpl::endEncodingImpl() {}

void CommandEncoderImpl::uploadBufferDataImpl(IBuffer* buffer, Offset offset, Size size, void* data)
{
    // Copy to staging buffer
    IBuffer* stagingBuffer = nullptr;
    Offset stagingBufferOffset = 0;
    m_commandBuffer->m_transientHeap
        ->allocateStagingBuffer(size, stagingBuffer, stagingBufferOffset, MemoryType::Upload);
    BufferImpl* stagingBufferImpl = static_cast<BufferImpl*>(stagingBuffer);
    BufferRange range = {stagingBufferOffset, size};
    void* mappedData;
    if (stagingBufferImpl->map(&range, &mappedData) == SLANG_OK)
    {
        memcpy((char*)mappedData + stagingBufferOffset, data, size);
        stagingBufferImpl->unmap(&range);
    }
    // Copy from staging buffer to real buffer
    m_device->m_ctx.api.wgpuCommandEncoderCopyBufferToBuffer(
        m_commandBuffer->m_commandEncoder,
        stagingBufferImpl->m_buffer,
        stagingBufferOffset,
        static_cast<BufferImpl*>(buffer)->m_buffer,
        offset,
        size
    );
}

Result CommandEncoderImpl::setPipelineImpl(IPipeline* state, IShaderObject** outRootObject)
{
    m_currentPipeline = static_cast<PipelineImpl*>(state);
    m_commandBuffer->m_mutableRootShaderObject = nullptr;
    SLANG_RETURN_ON_FAIL(m_commandBuffer->m_rootObject.init(
        m_commandBuffer->m_device,
        m_currentPipeline->getProgram<ShaderProgramImpl>()->m_rootObjectLayout
    ));
    *outRootObject = &m_commandBuffer->m_rootObject;
    return SLANG_OK;
}

Result CommandEncoderImpl::setPipelineWithRootObjectImpl(IPipeline* state, IShaderObject* rootObject)
{
    m_currentPipeline = static_cast<PipelineImpl*>(state);
    m_commandBuffer->m_mutableRootShaderObject = static_cast<MutableRootShaderObjectImpl*>(rootObject);
    return SLANG_OK;
}

void CommandEncoderImpl::textureBarrier(GfxCount count, ITexture* const* textures, ResourceState src, ResourceState dst)
{
    // WGPU doesn't have explicit barriers.
}

void CommandEncoderImpl::textureSubresourceBarrier(
    ITexture* texture,
    SubresourceRange subresourceRange,
    ResourceState src,
    ResourceState dst
)
{
    // WGPU doesn't have explicit barriers.
}

void CommandEncoderImpl::bufferBarrier(GfxCount count, IBuffer* const* buffers, ResourceState src, ResourceState dst)
{
    // WGPU doesn't have explicit barriers.
}

void CommandEncoderImpl::beginDebugEvent(const char* name, float rgbColor[3]) {}

void CommandEncoderImpl::endDebugEvent() {}

void CommandEncoderImpl::writeTimestamp(IQueryPool* pool, GfxIndex index) {}


void* ResourceCommandEncoderImpl::getInterface(SlangUUID const& uuid)
{
    if (uuid == GUID::IID_IResourceCommandEncoder || uuid == GUID::IID_ICommandEncoder ||
        uuid == ISlangUnknown::getTypeGuid())
    {
        return this;
    }
    return nullptr;
}

Result ResourceCommandEncoderImpl::init(CommandBufferImpl* commandBuffer)
{
    CommandEncoderImpl::init(commandBuffer);
    return SLANG_OK;
}

void ResourceCommandEncoderImpl::endEncoding()
{
    endEncodingImpl();
}

void ResourceCommandEncoderImpl::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    BufferImpl* dstBuffer = static_cast<BufferImpl*>(dst);
    BufferImpl* srcBuffer = static_cast<BufferImpl*>(src);
    m_device->m_ctx.api.wgpuCommandEncoderCopyBufferToBuffer(
        m_commandEncoder,
        srcBuffer->m_buffer,
        srcOffset,
        dstBuffer->m_buffer,
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
    TextureImpl* dstTexture = static_cast<TextureImpl*>(dst);
    TextureImpl* srcTexture = static_cast<TextureImpl*>(src);
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

void ResourceCommandEncoderImpl::clearBuffer(IBuffer* buffer, const BufferRange* range)
{
    BufferImpl* bufferImpl = static_cast<BufferImpl*>(buffer);
    uint64_t offset = range ? range->offset : 0;
    uint64_t size = range ? range->size : bufferImpl->m_desc.size;
    m_device->m_ctx.api.wgpuCommandEncoderClearBuffer(m_commandEncoder, bufferImpl->m_buffer, offset, size);
}

void ResourceCommandEncoderImpl::clearTexture(
    ITexture* texture,
    const ClearValue& clearValue,
    const SubresourceRange* subresourceRange,
    bool clearDepth,
    bool clearStencil
)
{
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
    SLANG_RHI_UNIMPLEMENTED("resolveQuery");
}

void* RenderCommandEncoderImpl::getInterface(SlangUUID const& uuid)
{
    if (uuid == GUID::IID_IRenderCommandEncoder || uuid == GUID::IID_ICommandEncoder ||
        uuid == ISlangUnknown::getTypeGuid())
    {
        return this;
    }
    return nullptr;
}

Result RenderCommandEncoderImpl::init(CommandBufferImpl* commandBuffer, const RenderPassDesc& desc)
{
    CommandEncoderImpl::init(commandBuffer);

    short_vector<WGPURenderPassColorAttachment, 8> colorAttachments(desc.colorAttachmentCount, {});
    for (GfxIndex i = 0; i < desc.colorAttachmentCount; ++i)
    {
        const RenderPassColorAttachment& attachmentIn = desc.colorAttachments[i];
        WGPURenderPassColorAttachment& attachment = colorAttachments[i];
        attachment.view = static_cast<TextureViewImpl*>(attachmentIn.view)->m_textureView;
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
        attachment.view = static_cast<TextureViewImpl*>(attachmentIn.view)->m_textureView;
        attachment.depthLoadOp = translateLoadOp(attachmentIn.depthLoadOp);
        attachment.depthStoreOp = translateStoreOp(attachmentIn.depthStoreOp);
        attachment.depthClearValue = attachmentIn.depthClearValue;
        attachment.depthReadOnly = attachmentIn.depthReadOnly;
        attachment.stencilLoadOp = translateLoadOp(attachmentIn.stencilLoadOp);
        attachment.stencilStoreOp = translateStoreOp(attachmentIn.stencilStoreOp);
        attachment.stencilClearValue = attachmentIn.stencilClearValue;
        attachment.stencilReadOnly = attachmentIn.stencilReadOnly;
    }

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = desc.colorAttachmentCount;
    passDesc.colorAttachments = colorAttachments.data();
    passDesc.depthStencilAttachment = desc.depthStencilAttachment ? &depthStencilAttachment : nullptr;
    // passDesc.occlusionQuerySet not supported
    // passDesc.timestampWrites not supported

    m_renderPassEncoder =
        m_device->m_ctx.api.wgpuCommandEncoderBeginRenderPass(m_commandBuffer->m_commandEncoder, &passDesc);
    return m_renderPassEncoder ? SLANG_OK : SLANG_FAIL;
}

Result RenderCommandEncoderImpl::prepareDraw()
{
    auto pipeline = static_cast<PipelineImpl*>(m_currentPipeline.Ptr());
    if (!pipeline)
    {
        return SLANG_FAIL;
    }

    RootBindingContext context;
    SLANG_RETURN_ON_FAIL(bindPipelineImpl(context));

    m_device->m_ctx.api.wgpuRenderPassEncoderSetPipeline(m_renderPassEncoder, m_currentPipeline->m_renderPipeline);
    for (uint32_t groupIndex = 0; groupIndex < context.bindGroups.size(); groupIndex++)
    {
        m_device->m_ctx.api.wgpuRenderPassEncoderSetBindGroup(
            m_renderPassEncoder,
            groupIndex,
            context.bindGroups[groupIndex],
            0,
            nullptr
        );
    }
    return SLANG_OK;
}

void RenderCommandEncoderImpl::endEncoding()
{
    endEncodingImpl();
    m_device->m_ctx.api.wgpuRenderPassEncoderEnd(m_renderPassEncoder);
    m_device->m_ctx.api.wgpuRenderPassEncoderRelease(m_renderPassEncoder);
    m_renderPassEncoder = nullptr;
}

Result RenderCommandEncoderImpl::bindPipeline(IPipeline* pipeline, IShaderObject** outRootObject)
{
    return setPipelineImpl(pipeline, outRootObject);
}

Result RenderCommandEncoderImpl::bindPipelineWithRootObject(IPipeline* pipeline, IShaderObject* rootObject)
{
    return setPipelineWithRootObjectImpl(pipeline, rootObject);
}

void RenderCommandEncoderImpl::setViewports(GfxCount count, const Viewport* viewports)
{
    if (count < 1)
        return;

    m_device->m_ctx.api.wgpuRenderPassEncoderSetViewport(
        m_renderPassEncoder,
        viewports[0].originX,
        viewports[0].originY,
        viewports[0].extentX,
        viewports[0].extentY,
        viewports[0].minZ,
        viewports[0].maxZ
    );
}

void RenderCommandEncoderImpl::setScissorRects(GfxCount count, const ScissorRect* rects)
{
    if (count < 1)
        return;

    m_device->m_ctx.api.wgpuRenderPassEncoderSetScissorRect(
        m_renderPassEncoder,
        rects[0].minX,
        rects[0].minY,
        rects[0].maxX - rects[0].minX,
        rects[0].maxY - rects[0].minY
    );
}

void RenderCommandEncoderImpl::setPrimitiveTopology(PrimitiveTopology topology) {}

void RenderCommandEncoderImpl::setVertexBuffers(
    GfxIndex startSlot,
    GfxCount slotCount,
    IBuffer* const* buffers,
    const Offset* offsets
)
{
    for (GfxCount i = 0; i < slotCount; i++)
    {
        BufferImpl* bufferImpl = static_cast<BufferImpl*>(buffers[i]);
        m_device->m_ctx.api.wgpuRenderPassEncoderSetVertexBuffer(
            m_renderPassEncoder,
            startSlot + i,
            bufferImpl->m_buffer,
            offsets[i],
            bufferImpl->m_desc.size - offsets[i]
        );
    }
}

void RenderCommandEncoderImpl::setIndexBuffer(IBuffer* buffer, Format indexFormat, Offset offset)
{
    BufferImpl* bufferImpl = static_cast<BufferImpl*>(buffer);
    m_device->m_ctx.api.wgpuRenderPassEncoderSetIndexBuffer(
        m_renderPassEncoder,
        bufferImpl->m_buffer,
        indexFormat == Format::R32_UINT ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16,
        offset,
        bufferImpl->m_desc.size - offset
    );
}

void RenderCommandEncoderImpl::setStencilReference(uint32_t referenceValue)
{
    m_device->m_ctx.api.wgpuRenderPassEncoderSetStencilReference(m_renderPassEncoder, referenceValue);
}

Result RenderCommandEncoderImpl::setSamplePositions(
    GfxCount samplesPerPixel,
    GfxCount pixelCount,
    const SamplePosition* samplePositions
)
{
    return SLANG_FAIL;
}

Result RenderCommandEncoderImpl::draw(GfxCount vertexCount, GfxIndex startVertex)
{
    SLANG_RETURN_ON_FAIL(prepareDraw());
    m_device->m_ctx.api.wgpuRenderPassEncoderDraw(m_renderPassEncoder, vertexCount, 1, startVertex, 0);
    return SLANG_OK;
}

Result RenderCommandEncoderImpl::drawIndexed(GfxCount indexCount, GfxIndex startIndex, GfxIndex baseVertex)
{
    SLANG_RETURN_ON_FAIL(prepareDraw());
    m_device->m_ctx.api.wgpuRenderPassEncoderDrawIndexed(m_renderPassEncoder, indexCount, 1, startIndex, baseVertex, 0);
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
    // m_device->m_ctx.api.wgpuRenderPassEncoderDrawIndirect(
    //     m_renderPassEncoder,
    //     static_cast<BufferImpl*>(argBuffer)->m_buffer,
    //     argOffset
    // );
    return SLANG_FAIL;
}

Result RenderCommandEncoderImpl::drawIndexedIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    // m_device->m_ctx.api.wgpuRenderPassEncoderDrawIndexedIndirect(
    //     m_renderPassEncoder,
    //     static_cast<BufferImpl*>(argBuffer)->m_buffer,
    //     argOffset
    // );
    return SLANG_FAIL;
}

Result RenderCommandEncoderImpl::drawInstanced(
    GfxCount vertexCount,
    GfxCount instanceCount,
    GfxIndex startVertex,
    GfxIndex startInstanceLocation
)
{
    SLANG_RETURN_ON_FAIL(prepareDraw());
    m_device->m_ctx.api
        .wgpuRenderPassEncoderDraw(m_renderPassEncoder, vertexCount, instanceCount, startVertex, startInstanceLocation);
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
    SLANG_RETURN_ON_FAIL(prepareDraw());
    m_device->m_ctx.api.wgpuRenderPassEncoderDrawIndexed(
        m_renderPassEncoder,
        indexCount,
        instanceCount,
        startIndexLocation,
        baseVertexLocation,
        startInstanceLocation
    );
    return SLANG_OK;
}

Result RenderCommandEncoderImpl::drawMeshTasks(int x, int y, int z)
{
    return SLANG_FAIL;
}


void* ComputeCommandEncoderImpl::getInterface(SlangUUID const& uuid)
{
    if (uuid == GUID::IID_IComputeCommandEncoder || uuid == GUID::IID_ICommandEncoder ||
        uuid == ISlangUnknown::getTypeGuid())
    {
        return this;
    }
    return nullptr;
}

Result ComputeCommandEncoderImpl::init(CommandBufferImpl* commandBuffer)
{
    CommandEncoderImpl::init(commandBuffer);
    WGPUComputePassDescriptor passDesc = {};
    m_computePassEncoder =
        m_device->m_ctx.api.wgpuCommandEncoderBeginComputePass(m_commandBuffer->m_commandEncoder, &passDesc);
    return m_computePassEncoder ? SLANG_OK : SLANG_FAIL;
}

void ComputeCommandEncoderImpl::endEncoding()
{
    endEncodingImpl();
    m_device->m_ctx.api.wgpuComputePassEncoderEnd(m_computePassEncoder);
    m_device->m_ctx.api.wgpuComputePassEncoderRelease(m_computePassEncoder);
    m_computePassEncoder = nullptr;
}

Result ComputeCommandEncoderImpl::bindPipeline(IPipeline* pipeline, IShaderObject** outRootObject)
{
    return setPipelineImpl(pipeline, outRootObject);
}

Result ComputeCommandEncoderImpl::bindPipelineWithRootObject(IPipeline* pipeline, IShaderObject* rootObject)
{
    return setPipelineWithRootObjectImpl(pipeline, rootObject);
}

Result ComputeCommandEncoderImpl::dispatchCompute(int x, int y, int z)
{
    auto pipeline = static_cast<PipelineImpl*>(m_currentPipeline.Ptr());
    if (!pipeline)
    {
        return SLANG_FAIL;
    }

    RootBindingContext context;
    SLANG_RETURN_ON_FAIL(bindPipelineImpl(context));

    m_device->m_ctx.api.wgpuComputePassEncoderSetPipeline(m_computePassEncoder, m_currentPipeline->m_computePipeline);
    for (uint32_t groupIndex = 0; groupIndex < context.bindGroups.size(); groupIndex++)
    {
        m_device->m_ctx.api.wgpuComputePassEncoderSetBindGroup(
            m_computePassEncoder,
            groupIndex,
            context.bindGroups[groupIndex],
            0,
            nullptr
        );
    }
    m_device->m_ctx.api.wgpuComputePassEncoderDispatchWorkgroups(m_computePassEncoder, x, y, z);
    return SLANG_OK;
}

Result ComputeCommandEncoderImpl::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    m_device->m_ctx.api.wgpuComputePassEncoderDispatchWorkgroupsIndirect(
        m_computePassEncoder,
        static_cast<BufferImpl*>(argBuffer)->m_buffer,
        offset
    );
    return SLANG_OK;
}

} // namespace rhi::wgpu
