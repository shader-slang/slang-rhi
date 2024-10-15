#include "wgpu-command-encoder.h"
#include "wgpu-command-buffer.h"
#include "wgpu-command-queue.h"
#include "wgpu-pipeline.h"
#include "wgpu-shader-program.h"
#include "wgpu-buffer.h"
#include "wgpu-texture.h"
#include "wgpu-device.h"
#include "wgpu-transient-resource-heap.h"
#include "wgpu-util.h"

namespace rhi::wgpu {

Result CommandEncoderImpl::init(DeviceImpl* device, CommandQueueImpl* queue)
{
    m_device = device;
    m_commandBuffer = new CommandBufferImpl();
    m_commandEncoder = m_device->m_ctx.api.wgpuDeviceCreateCommandEncoder(m_device->m_ctx.device, nullptr);
    if (!m_commandEncoder)
    {
        return SLANG_FAIL;
    }

    m_transientHeap = new TransientResourceHeapImpl();
    SLANG_RETURN_ON_FAIL(m_transientHeap->init({}, m_device));

    return SLANG_OK;
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
    BufferImpl* dstBuffer = checked_cast<BufferImpl*>(dst);
    BufferImpl* srcBuffer = checked_cast<BufferImpl*>(src);
    m_device->m_ctx.api.wgpuCommandEncoderCopyBufferToBuffer(
        m_commandEncoder,
        srcBuffer->m_buffer,
        srcOffset,
        dstBuffer->m_buffer,
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
    SLANG_UNUSED(dst);
    SLANG_UNUSED(dstSubresource);
    SLANG_UNUSED(dstOffset);
    SLANG_UNUSED(src);
    SLANG_UNUSED(srcSubresource);
    SLANG_UNUSED(srcOffset);
    SLANG_UNUSED(extent);
    SLANG_RHI_UNIMPLEMENTED("copyTexture");
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
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    uint64_t offset = range ? range->offset : 0;
    uint64_t size = range ? range->size : bufferImpl->m_desc.size;
    m_device->m_ctx.api.wgpuCommandEncoderClearBuffer(m_commandEncoder, bufferImpl->m_buffer, offset, size);
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
    SLANG_RHI_UNIMPLEMENTED("clearTexture");
}

void CommandEncoderImpl::resolveQuery(
    IQueryPool* queryPool,
    GfxIndex index,
    GfxCount count,
    IBuffer* buffer,
    Offset offset
)
{
    SLANG_UNUSED(queryPool);
    SLANG_UNUSED(index);
    SLANG_UNUSED(count);
    SLANG_UNUSED(buffer);
    SLANG_UNUSED(offset);
    SLANG_RHI_UNIMPLEMENTED("resolveQuery");
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
    SLANG_UNUSED(dst);
    SLANG_UNUSED(dstOffset);
    SLANG_UNUSED(dstSize);
    SLANG_UNUSED(dstRowStride);
    SLANG_UNUSED(src);
    SLANG_UNUSED(srcSubresource);
    SLANG_UNUSED(srcOffset);
    SLANG_UNUSED(extent);
    SLANG_RHI_UNIMPLEMENTED("copyTextureToBuffer");
}

void CommandEncoderImpl::beginRenderPass(const RenderPassDesc& desc)
{
    short_vector<WGPURenderPassColorAttachment, 8> colorAttachments(desc.colorAttachmentCount, {});
    for (GfxIndex i = 0; i < desc.colorAttachmentCount; ++i)
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

    endPassEncoder();
    m_renderPassEncoder = m_device->m_ctx.api.wgpuCommandEncoderBeginRenderPass(m_commandEncoder, &passDesc);
}

void CommandEncoderImpl::endRenderPass()
{
    endPassEncoder();
}

template<typename T>
inline bool arraysEqual(GfxCount countA, GfxCount countB, const T* a, const T* b)
{
    return (countA == countB) ? std::memcmp(a, b, countA * sizeof(T)) == 0 : false;
}

void CommandEncoderImpl::setRenderState(const RenderState& state)
{
    if (!m_renderPassEncoder)
        return;

    bool updatePipeline = !m_renderStateValid || state.pipeline != m_renderState.pipeline;
    bool updateRootObject = updatePipeline || state.rootObject != m_renderState.rootObject;
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
        m_renderPipeline = checked_cast<RenderPipelineImpl*>(state.pipeline);
        m_device->m_ctx.api.wgpuRenderPassEncoderSetPipeline(encoder, m_renderPipeline->m_renderPipeline);
    }

    if (updateRootObject)
    {
        m_rootObject = checked_cast<RootShaderObjectImpl*>(state.rootObject);
        auto specializedLayout = m_rootObject->getSpecializedLayout();
        RootBindingContext bindingContext;
        bindingContext.device = m_device;
        bindingContext.bindGroupLayouts = specializedLayout->m_bindGroupLayouts;
        m_rootObject->bindAsRoot(this, bindingContext, m_renderPipeline->m_rootObjectLayout);
        for (uint32_t groupIndex = 0; groupIndex < bindingContext.bindGroups.size(); groupIndex++)
        {
            m_device->m_ctx.api.wgpuRenderPassEncoderSetBindGroup(
                m_renderPassEncoder,
                groupIndex,
                bindingContext.bindGroups[groupIndex],
                0,
                nullptr
            );
        }
    }

    if (updateStencilRef)
    {
        m_device->m_ctx.api.wgpuRenderPassEncoderSetStencilReference(m_renderPassEncoder, state.stencilRef);
    }

    if (updateVertexBuffers)
    {
        for (Index i = 0; i < state.vertexBufferCount; ++i)
        {
            BufferImpl* buffer = checked_cast<BufferImpl*>(state.vertexBuffers[i].buffer);
            Offset offset = state.vertexBuffers[i].offset;
            m_device->m_ctx.api.wgpuRenderPassEncoderSetVertexBuffer(
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
            Offset offset = state.indexBuffer.offset;
            m_device->m_ctx.api.wgpuRenderPassEncoderSetIndexBuffer(
                m_renderPassEncoder,
                buffer->m_buffer,
                state.indexFormat == IndexFormat::UInt32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16,
                offset,
                buffer->m_desc.size - offset
            );
        }
    }

    if (updateViewports && state.viewportCount > 0)
    {
        const Viewport& viewport = state.viewports[0];
        m_device->m_ctx.api.wgpuRenderPassEncoderSetViewport(
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
        m_device->m_ctx.api.wgpuRenderPassEncoderSetScissorRect(
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
    m_computeState = {};
    m_computePipeline = nullptr;
}

void CommandEncoderImpl::draw(const DrawArguments& args)
{
    if (!m_renderStateValid)
        return;

    m_device->m_ctx.api.wgpuRenderPassEncoderDraw(
        m_renderPassEncoder,
        args.vertexCount,
        args.instanceCount,
        args.startVertexLocation,
        args.startInstanceLocation
    );
}

void CommandEncoderImpl::drawIndexed(const DrawArguments& args)
{
    if (!m_renderStateValid)
        return;

    m_device->m_ctx.api.wgpuRenderPassEncoderDrawIndexed(
        m_renderPassEncoder,
        args.vertexCount,
        args.instanceCount,
        args.startIndexLocation,
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
    if (!m_renderStateValid)
        return;

    m_device->m_ctx.api.wgpuRenderPassEncoderMultiDrawIndirect(
        m_renderPassEncoder,
        checked_cast<BufferImpl*>(argBuffer)->m_buffer,
        argOffset,
        maxDrawCount,
        countBuffer ? checked_cast<BufferImpl*>(countBuffer)->m_buffer : nullptr,
        countOffset
    );
}

void CommandEncoderImpl::drawIndexedIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    if (!m_renderStateValid)
        return;

    m_device->m_ctx.api.wgpuRenderPassEncoderMultiDrawIndexedIndirect(
        m_renderPassEncoder,
        checked_cast<BufferImpl*>(argBuffer)->m_buffer,
        argOffset,
        maxDrawCount,
        countBuffer ? checked_cast<BufferImpl*>(countBuffer)->m_buffer : nullptr,
        countOffset
    );
}

void CommandEncoderImpl::drawMeshTasks(int x, int y, int z)
{
    SLANG_UNUSED(x);
    SLANG_UNUSED(y);
    SLANG_UNUSED(z);
}

void CommandEncoderImpl::setComputeState(const ComputeState& state)
{
    if (m_renderPassEncoder)
        return;

    bool updatePipeline = !m_computeStateValid || state.pipeline != m_computeState.pipeline;
    bool updateRootObject = updatePipeline || state.rootObject != m_computeState.rootObject;

    if (!m_computePassEncoder)
    {
        m_computePassEncoder = m_device->m_ctx.api.wgpuCommandEncoderBeginComputePass(m_commandEncoder, nullptr);
    }

    WGPUComputePassEncoder encoder = m_computePassEncoder;

    if (updatePipeline)
    {
        m_computePipeline = checked_cast<ComputePipelineImpl*>(state.pipeline);
        m_device->m_ctx.api.wgpuComputePassEncoderSetPipeline(encoder, m_computePipeline->m_computePipeline);
    }

    if (updateRootObject)
    {
        m_rootObject = checked_cast<RootShaderObjectImpl*>(state.rootObject);
        auto specializedLayout = m_rootObject->getSpecializedLayout();
        RootBindingContext bindingContext;
        bindingContext.device = m_device;
        bindingContext.bindGroupLayouts = specializedLayout->m_bindGroupLayouts;
        m_rootObject->bindAsRoot(this, bindingContext, specializedLayout);
        for (uint32_t groupIndex = 0; groupIndex < bindingContext.bindGroups.size(); groupIndex++)
        {
            m_device->m_ctx.api.wgpuComputePassEncoderSetBindGroup(
                m_computePassEncoder,
                groupIndex,
                bindingContext.bindGroups[groupIndex],
                0,
                nullptr
            );
        }
    }

    m_computeStateValid = true;
    m_computeState = state;
}

void CommandEncoderImpl::dispatchCompute(int x, int y, int z)
{
    if (!m_computeStateValid)
        return;

    m_device->m_ctx.api.wgpuComputePassEncoderDispatchWorkgroups(m_computePassEncoder, x, y, z);
}

void CommandEncoderImpl::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    if (!m_computeStateValid)
        return;

    m_device->m_ctx.api.wgpuComputePassEncoderDispatchWorkgroupsIndirect(
        m_computePassEncoder,
        checked_cast<BufferImpl*>(argBuffer)->m_buffer,
        offset
    );
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
    SLANG_UNUSED(desc);
    SLANG_UNUSED(dst);
    SLANG_UNUSED(src);
    SLANG_UNUSED(scratchBuffer);
    SLANG_UNUSED(propertyQueryCount);
    SLANG_UNUSED(queryDescs);
    SLANG_RHI_UNIMPLEMENTED("buildAccelerationStructure");
}

void CommandEncoderImpl::copyAccelerationStructure(
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    AccelerationStructureCopyMode mode
)
{
    SLANG_UNUSED(dst);
    SLANG_UNUSED(src);
    SLANG_UNUSED(mode);
    SLANG_RHI_UNIMPLEMENTED("copyAccelerationStructure");
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
    m_device->m_ctx.api.wgpuCommandEncoderPushDebugGroup(m_commandEncoder, name);
}

void CommandEncoderImpl::endDebugEvent()
{
    m_device->m_ctx.api.wgpuCommandEncoderPopDebugGroup(m_commandEncoder);
}

void CommandEncoderImpl::writeTimestamp(IQueryPool* pool, GfxIndex index)
{
    SLANG_UNUSED(pool);
    SLANG_UNUSED(index);
    SLANG_RHI_UNIMPLEMENTED("writeTimestamp");
}

Result CommandEncoderImpl::finish(ICommandBuffer** outCommandBuffer)
{
    if (!m_commandBuffer)
    {
        return SLANG_FAIL;
    }
    endPassEncoder();
    WGPUCommandBufferDescriptor desc = {};
    m_commandBuffer->m_commandBuffer = m_device->m_ctx.api.wgpuCommandEncoderFinish(m_commandEncoder, &desc);
    returnComPtr(outCommandBuffer, m_commandBuffer);
    m_commandBuffer = nullptr;
    return SLANG_OK;
}

Result CommandEncoderImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::WGPUCommandEncoder;
    outHandle->value = (uint64_t)m_commandEncoder;
    return SLANG_OK;
}

void CommandEncoderImpl::endPassEncoder()
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
        m_computeState = {};
        m_computePipeline = nullptr;
    }

    m_rootObject = nullptr;
}

} // namespace rhi::wgpu
