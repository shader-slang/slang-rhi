#include "debug-command-encoder.h"
#include "debug-buffer.h"
#include "debug-command-buffer.h"
#include "debug-helper-functions.h"
#include "debug-pipeline.h"
#include "debug-query.h"
#include "debug-texture.h"
#include "debug-texture-view.h"

#include <vector>

namespace rhi::debug {

// DebugPassEncoder

void DebugPassEncoder::setBufferState(IBuffer* buffer, ResourceState state)
{
    SLANG_RHI_API_FUNC;
    getBaseObject()->setBufferState(getInnerObj(buffer), state);
}

void DebugPassEncoder::setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state)
{
    SLANG_RHI_API_FUNC;
    getBaseObject()->setTextureState(getInnerObj(texture), subresourceRange, state);
}

void DebugPassEncoder::beginDebugEvent(const char* name, float rgbColor[3])
{
    SLANG_RHI_API_FUNC;
    getBaseObject()->beginDebugEvent(name, rgbColor);
}

void DebugPassEncoder::endDebugEvent()
{
    SLANG_RHI_API_FUNC;
    getBaseObject()->endDebugEvent();
}

void DebugPassEncoder::writeTimestamp(IQueryPool* pool, GfxIndex index)
{
    getBaseObject()->writeTimestamp(getInnerObj(pool), index);
}

// DebugResourcePassEncoder

void DebugResourcePassEncoder::end()
{
    SLANG_RHI_API_FUNC;
    isOpen = false;
    baseObject->end();
}

void DebugResourcePassEncoder::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    SLANG_RHI_API_FUNC;
    auto dstImpl = checked_cast<DebugBuffer*>(dst);
    auto srcImpl = checked_cast<DebugBuffer*>(src);
    baseObject->copyBuffer(dstImpl->baseObject, dstOffset, srcImpl->baseObject, srcOffset, size);
}

void DebugResourcePassEncoder::uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data)
{
    SLANG_RHI_API_FUNC;
    auto dstImpl = checked_cast<DebugBuffer*>(dst);
    baseObject->uploadBufferData(dstImpl->baseObject, offset, size, data);
}

void DebugResourcePassEncoder::copyTexture(
    ITexture* dst,
    SubresourceRange dstSubresource,
    Offset3D dstOffset,
    ITexture* src,
    SubresourceRange srcSubresource,
    Offset3D srcOffset,
    Extents extent
)
{
    SLANG_RHI_API_FUNC;
    baseObject
        ->copyTexture(getInnerObj(dst), dstSubresource, dstOffset, getInnerObj(src), srcSubresource, srcOffset, extent);
}

void DebugResourcePassEncoder::uploadTextureData(
    ITexture* dst,
    SubresourceRange subresourceRange,
    Offset3D offset,
    Extents extent,
    SubresourceData* subresourceData,
    GfxCount subresourceDataCount
)
{
    SLANG_RHI_API_FUNC;
    baseObject
        ->uploadTextureData(getInnerObj(dst), subresourceRange, offset, extent, subresourceData, subresourceDataCount);
}

void DebugResourcePassEncoder::clearBuffer(IBuffer* buffer, const BufferRange* range)
{
    SLANG_RHI_API_FUNC;
    baseObject->clearBuffer(getInnerObj(buffer), range);
}

void DebugResourcePassEncoder::clearTexture(
    ITexture* texture,
    const ClearValue& clearValue,
    const SubresourceRange* subresourceRange,
    bool clearDepth,
    bool clearStencil
)
{
    SLANG_RHI_API_FUNC;
    baseObject->clearTexture(getInnerObj(texture), clearValue, subresourceRange, clearDepth, clearStencil);
}

void DebugResourcePassEncoder::resolveQuery(
    IQueryPool* queryPool,
    GfxIndex index,
    GfxCount count,
    IBuffer* buffer,
    Offset offset
)
{
    SLANG_RHI_API_FUNC;
    baseObject->resolveQuery(getInnerObj(queryPool), index, count, getInnerObj(buffer), offset);
}

void DebugResourcePassEncoder::copyTextureToBuffer(
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
    SLANG_RHI_API_FUNC;
    baseObject->copyTextureToBuffer(
        getInnerObj(dst),
        dstOffset,
        dstSize,
        dstRowStride,
        getInnerObj(src),
        srcSubresource,
        srcOffset,
        extent
    );
}

// DebugRenderPassEncoder

void DebugRenderPassEncoder::end()
{
    SLANG_RHI_API_FUNC;
    isOpen = false;
    baseObject->end();
}

Result DebugRenderPassEncoder::bindPipeline(IPipeline* state, IShaderObject** outRootShaderObject)
{
    SLANG_RHI_API_FUNC;

    auto innerState = getInnerObj(state);
    IShaderObject* innerRootObject = nullptr;
    commandEncoder->rootObject.reset();
    auto result = baseObject->bindPipeline(innerState, &innerRootObject);
    commandEncoder->rootObject.baseObject.attach(innerRootObject);
    *outRootShaderObject = &commandEncoder->rootObject;
    return result;
}

Result DebugRenderPassEncoder::bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject)
{
    SLANG_RHI_API_FUNC;
    return baseObject->bindPipelineWithRootObject(getInnerObj(state), getInnerObj(rootObject));
}

void DebugRenderPassEncoder::setViewports(GfxCount count, const Viewport* viewports)
{
    SLANG_RHI_API_FUNC;
    baseObject->setViewports(count, viewports);
}

void DebugRenderPassEncoder::setScissorRects(GfxCount count, const ScissorRect* scissors)
{
    SLANG_RHI_API_FUNC;
    baseObject->setScissorRects(count, scissors);
}

void DebugRenderPassEncoder::setVertexBuffers(
    GfxIndex startSlot,
    GfxCount slotCount,
    IBuffer* const* buffers,
    const Offset* offsets
)
{
    SLANG_RHI_API_FUNC;

    std::vector<IBuffer*> innerBuffers;
    for (GfxIndex i = 0; i < slotCount; i++)
    {
        innerBuffers.push_back(checked_cast<DebugBuffer*>(buffers[i])->baseObject.get());
    }
    baseObject->setVertexBuffers(startSlot, slotCount, innerBuffers.data(), offsets);
}

void DebugRenderPassEncoder::setIndexBuffer(IBuffer* buffer, IndexFormat indexFormat, Offset offset)
{
    SLANG_RHI_API_FUNC;
    auto innerBuffer = checked_cast<DebugBuffer*>(buffer)->baseObject.get();
    baseObject->setIndexBuffer(innerBuffer, indexFormat, offset);
}

Result DebugRenderPassEncoder::draw(GfxCount vertexCount, GfxIndex startVertex)
{
    SLANG_RHI_API_FUNC;
    return baseObject->draw(vertexCount, startVertex);
}

Result DebugRenderPassEncoder::drawIndexed(GfxCount indexCount, GfxIndex startIndex, GfxIndex baseVertex)
{
    SLANG_RHI_API_FUNC;
    return baseObject->drawIndexed(indexCount, startIndex, baseVertex);
}

Result DebugRenderPassEncoder::drawIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    SLANG_RHI_API_FUNC;
    return baseObject
        ->drawIndirect(maxDrawCount, getInnerObj(argBuffer), argOffset, getInnerObj(countBuffer), countOffset);
}

Result DebugRenderPassEncoder::drawIndexedIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    SLANG_RHI_API_FUNC;
    return baseObject
        ->drawIndexedIndirect(maxDrawCount, getInnerObj(argBuffer), argOffset, getInnerObj(countBuffer), countOffset);
}

void DebugRenderPassEncoder::setStencilReference(uint32_t referenceValue)
{
    SLANG_RHI_API_FUNC;
    return baseObject->setStencilReference(referenceValue);
}

Result DebugRenderPassEncoder::setSamplePositions(
    GfxCount samplesPerPixel,
    GfxCount pixelCount,
    const SamplePosition* samplePositions
)
{
    SLANG_RHI_API_FUNC;
    return baseObject->setSamplePositions(samplesPerPixel, pixelCount, samplePositions);
}

Result DebugRenderPassEncoder::drawInstanced(
    GfxCount vertexCount,
    GfxCount instanceCount,
    GfxIndex startVertex,
    GfxIndex startInstanceLocation
)
{
    SLANG_RHI_API_FUNC;
    return baseObject->drawInstanced(vertexCount, instanceCount, startVertex, startInstanceLocation);
}

Result DebugRenderPassEncoder::drawIndexedInstanced(
    GfxCount indexCount,
    GfxCount instanceCount,
    GfxIndex startIndexLocation,
    GfxIndex baseVertexLocation,
    GfxIndex startInstanceLocation
)
{
    SLANG_RHI_API_FUNC;
    return baseObject->drawIndexedInstanced(
        indexCount,
        instanceCount,
        startIndexLocation,
        baseVertexLocation,
        startInstanceLocation
    );
}

Result DebugRenderPassEncoder::drawMeshTasks(int x, int y, int z)
{
    SLANG_RHI_API_FUNC;
    return baseObject->drawMeshTasks(x, y, z);
}

// DebugComputePassEncoder

void DebugComputePassEncoder::end()
{
    SLANG_RHI_API_FUNC;
    isOpen = false;
    baseObject->end();
}

Result DebugComputePassEncoder::bindPipeline(IPipeline* state, IShaderObject** outRootShaderObject)
{
    SLANG_RHI_API_FUNC;

    auto innerState = getInnerObj(state);
    IShaderObject* innerRootObject = nullptr;
    commandEncoder->rootObject.reset();
    auto result = baseObject->bindPipeline(innerState, &innerRootObject);
    commandEncoder->rootObject.baseObject.attach(innerRootObject);
    *outRootShaderObject = &commandEncoder->rootObject;
    return result;
}

Result DebugComputePassEncoder::bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject)
{
    SLANG_RHI_API_FUNC;
    return baseObject->bindPipelineWithRootObject(getInnerObj(state), getInnerObj(rootObject));
}

Result DebugComputePassEncoder::dispatchCompute(int x, int y, int z)
{
    SLANG_RHI_API_FUNC;
    return baseObject->dispatchCompute(x, y, z);
}

Result DebugComputePassEncoder::dispatchComputeIndirect(IBuffer* cmdBuffer, Offset offset)
{
    SLANG_RHI_API_FUNC;
    return baseObject->dispatchComputeIndirect(getInnerObj(cmdBuffer), offset);
}

// DebugRayTracingPassEncoder

void DebugRayTracingPassEncoder::end()
{
    SLANG_RHI_API_FUNC;
    isOpen = false;
    baseObject->end();
}

void DebugRayTracingPassEncoder::buildAccelerationStructure(
    const AccelerationStructureBuildDesc& desc,
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    BufferWithOffset scratchBuffer,
    GfxCount propertyQueryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    SLANG_RHI_API_FUNC;
    AccelerationStructureBuildDesc innerDesc = desc;
    for (GfxIndex i = 0; i < innerDesc.inputCount; ++i)
    {
        switch ((AccelerationStructureBuildInputType&)(innerDesc.inputs[i]))
        {
        case AccelerationStructureBuildInputType::Instances:
        {
            AccelerationStructureBuildInputInstances& instances =
                (AccelerationStructureBuildInputInstances&)innerDesc.inputs[i];
            instances.instanceBuffer = getInnerObj(instances.instanceBuffer);
            break;
        }
        case AccelerationStructureBuildInputType::Triangles:
        {
            AccelerationStructureBuildInputTriangles& triangles =
                (AccelerationStructureBuildInputTriangles&)innerDesc.inputs[i];
            for (GfxIndex j = 0; j < triangles.vertexBufferCount; ++j)
            {
                triangles.vertexBuffers[j] = getInnerObj(triangles.vertexBuffers[j]);
            }
            triangles.indexBuffer = getInnerObj(triangles.indexBuffer);
            triangles.preTransformBuffer = getInnerObj(triangles.preTransformBuffer);
            break;
        }
        case AccelerationStructureBuildInputType::ProceduralPrimitives:
        {
            AccelerationStructureBuildInputProceduralPrimitives& proceduralPrimitives =
                (AccelerationStructureBuildInputProceduralPrimitives&)innerDesc.inputs[i];
            for (GfxIndex j = 0; j < proceduralPrimitives.aabbBufferCount; ++j)
            {
                proceduralPrimitives.aabbBuffers[j] = getInnerObj(proceduralPrimitives.aabbBuffers[j]);
            }
            break;
        }
        }
    }
    std::vector<AccelerationStructureQueryDesc> innerQueryDescs;
    for (size_t i = 0; i < propertyQueryCount; ++i)
    {
        innerQueryDescs.push_back(queryDescs[i]);
    }
    for (auto& innerQueryDesc : innerQueryDescs)
    {
        innerQueryDesc.queryPool = getInnerObj(innerQueryDesc.queryPool);
    }
    validateAccelerationStructureBuildDesc(ctx, desc);
    baseObject->buildAccelerationStructure(
        innerDesc,
        getInnerObj(dst),
        getInnerObj(src),
        getInnerObj(scratchBuffer),
        propertyQueryCount,
        innerQueryDescs.data()
    );
}

void DebugRayTracingPassEncoder::copyAccelerationStructure(
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    AccelerationStructureCopyMode mode
)
{
    SLANG_RHI_API_FUNC;
    auto innerDst = getInnerObj(dst);
    auto innerSrc = getInnerObj(src);
    baseObject->copyAccelerationStructure(innerDst, innerSrc, mode);
}

void DebugRayTracingPassEncoder::queryAccelerationStructureProperties(
    GfxCount accelerationStructureCount,
    IAccelerationStructure* const* accelerationStructures,
    GfxCount queryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    SLANG_RHI_API_FUNC;
    std::vector<IAccelerationStructure*> innerAS;
    for (GfxIndex i = 0; i < accelerationStructureCount; i++)
    {
        innerAS.push_back(getInnerObj(accelerationStructures[i]));
    }
    std::vector<AccelerationStructureQueryDesc> innerQueryDescs;
    for (size_t i = 0; i < queryCount; ++i)
    {
        innerQueryDescs.push_back(queryDescs[i]);
    }
    for (auto& innerQueryDesc : innerQueryDescs)
    {
        innerQueryDesc.queryPool = getInnerObj(innerQueryDesc.queryPool);
    }
    baseObject->queryAccelerationStructureProperties(
        accelerationStructureCount,
        innerAS.data(),
        queryCount,
        innerQueryDescs.data()
    );
}

void DebugRayTracingPassEncoder::serializeAccelerationStructure(BufferWithOffset dst, IAccelerationStructure* src)
{
    SLANG_RHI_API_FUNC;
    baseObject->serializeAccelerationStructure(getInnerObj(dst), getInnerObj(src));
}

void DebugRayTracingPassEncoder::deserializeAccelerationStructure(IAccelerationStructure* dst, BufferWithOffset src)
{
    SLANG_RHI_API_FUNC;
    baseObject->deserializeAccelerationStructure(getInnerObj(dst), getInnerObj(src));
}

Result DebugRayTracingPassEncoder::bindPipeline(IPipeline* state, IShaderObject** outRootObject)
{
    SLANG_RHI_API_FUNC;
    auto innerPipeline = getInnerObj(state);
    IShaderObject* innerRootObject = nullptr;
    commandEncoder->rootObject.reset();
    Result result = baseObject->bindPipeline(innerPipeline, &innerRootObject);
    commandEncoder->rootObject.baseObject.attach(innerRootObject);
    *outRootObject = &commandEncoder->rootObject;
    return result;
}

Result DebugRayTracingPassEncoder::bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject)
{
    SLANG_RHI_API_FUNC;
    return baseObject->bindPipelineWithRootObject(getInnerObj(state), getInnerObj(rootObject));
}

Result DebugRayTracingPassEncoder::dispatchRays(
    GfxIndex rayGenShaderIndex,
    IShaderTable* shaderTable,
    GfxCount width,
    GfxCount height,
    GfxCount depth
)
{
    SLANG_RHI_API_FUNC;
    return baseObject->dispatchRays(rayGenShaderIndex, getInnerObj(shaderTable), width, height, depth);
}

DebugCommandEncoder::DebugCommandEncoder(DebugContext* ctx)
    : DebugObject(ctx)
    , m_renderPassEncoder(ctx)
    , m_computePassEncoder(ctx)
    , m_resourcePassEncoder(ctx)
    , m_rayTracingPassEncoder(ctx)
{
    SLANG_RHI_API_FUNC;
    m_renderPassEncoder.commandEncoder = this;
    m_computePassEncoder.commandEncoder = this;
    m_resourcePassEncoder.commandEncoder = this;
    m_rayTracingPassEncoder.commandEncoder = this;
}

Result DebugCommandEncoder::beginResourcePass(IResourcePassEncoder** outEncoder)
{
    SLANG_RHI_API_FUNC;
    checkCommandBufferOpenWhenCreatingEncoder();
    checkEncodersClosedBeforeNewEncoder();
    m_resourcePassEncoder.isOpen = true;
    SLANG_RETURN_ON_FAIL(baseObject->beginResourcePass(&m_resourcePassEncoder.baseObject));
    *outEncoder = &m_resourcePassEncoder;
    return SLANG_OK;
}

Result DebugCommandEncoder::beginRenderPass(const RenderPassDesc& desc, IRenderPassEncoder** outEncoder)
{
    // TODO VALIDATION: resolveTarget must have usage RenderTarget (Vulkan, WGPU)

    SLANG_RHI_API_FUNC;
    checkCommandBufferOpenWhenCreatingEncoder();
    checkEncodersClosedBeforeNewEncoder();
    RenderPassDesc innerDesc = desc;
    short_vector<RenderPassColorAttachment> innerColorAttachments;
    for (Index i = 0; i < desc.colorAttachmentCount; ++i)
    {
        innerColorAttachments.push_back(desc.colorAttachments[i]);
        innerColorAttachments[i].view = getInnerObj(desc.colorAttachments[i].view);
        innerColorAttachments[i].resolveTarget = getInnerObj(desc.colorAttachments[i].resolveTarget);
    }
    innerDesc.colorAttachments = innerColorAttachments.data();
    RenderPassDepthStencilAttachment innerDepthStencilAttachment;
    if (desc.depthStencilAttachment)
    {
        innerDepthStencilAttachment = *desc.depthStencilAttachment;
        innerDepthStencilAttachment.view = getInnerObj(desc.depthStencilAttachment->view);
        innerDesc.depthStencilAttachment = &innerDepthStencilAttachment;
    }
    m_renderPassEncoder.isOpen = true;
    SLANG_RETURN_ON_FAIL(baseObject->beginRenderPass(innerDesc, &m_renderPassEncoder.baseObject));
    *outEncoder = &m_renderPassEncoder;
    return SLANG_OK;
}

Result DebugCommandEncoder::beginComputePass(IComputePassEncoder** outEncoder)
{
    SLANG_RHI_API_FUNC;
    checkCommandBufferOpenWhenCreatingEncoder();
    checkEncodersClosedBeforeNewEncoder();
    m_computePassEncoder.isOpen = true;
    SLANG_RETURN_ON_FAIL(baseObject->beginComputePass(&m_computePassEncoder.baseObject));
    *outEncoder = &m_computePassEncoder;
    return SLANG_OK;
}

Result DebugCommandEncoder::beginRayTracingPass(IRayTracingPassEncoder** outEncoder)
{
    SLANG_RHI_API_FUNC;
    checkCommandBufferOpenWhenCreatingEncoder();
    checkEncodersClosedBeforeNewEncoder();
    m_rayTracingPassEncoder.isOpen = true;
    SLANG_RETURN_ON_FAIL(baseObject->beginRayTracingPass(&m_rayTracingPassEncoder.baseObject));
    *outEncoder = &m_rayTracingPassEncoder;
    return SLANG_OK;
}

Result DebugCommandEncoder::finish(ICommandBuffer** outCommandBuffer)
{
    SLANG_RHI_API_FUNC;
    checkEncodersClosedBeforeFinish();
    RefPtr<DebugCommandBuffer> outObject = new DebugCommandBuffer(ctx);
    auto result = baseObject->finish(outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outCommandBuffer, outObject);
    return result;
}

Result DebugCommandEncoder::finishAndSubmit(ICommandBuffer** outCommandBuffer)
{
    SLANG_RHI_API_FUNC;
    checkEncodersClosedBeforeFinish();
    RefPtr<DebugCommandBuffer> outObject = new DebugCommandBuffer(ctx);
    auto result = baseObject->finishAndSubmit(outObject->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outCommandBuffer, outObject);
    return result;
}

Result DebugCommandEncoder::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getNativeHandle(outHandle);
}

#if 0
void DebugCommandEncoder::invalidateDescriptorHeapBinding()
{
    SLANG_RHI_API_FUNC;
    ComPtr<ICommandBufferD3D12> cmdBuf;
    if (SLANG_FAILED(baseObject->queryInterface(ICommandBufferD3D12::getTypeGuid(), (void**)cmdBuf.writeRef())))
    {
        RHI_VALIDATION_ERROR("The current command buffer implementation does not provide ICommandBufferD3D12 interface."
        );
        return;
    }
    return cmdBuf->invalidateDescriptorHeapBinding();
}

void DebugCommandEncoder::ensureInternalDescriptorHeapsBound()
{
    SLANG_RHI_API_FUNC;
    ComPtr<ICommandBufferD3D12> cmdBuf;
    if (SLANG_FAILED(baseObject->queryInterface(ICommandBufferD3D12::getTypeGuid(), (void**)cmdBuf.writeRef())))
    {
        RHI_VALIDATION_ERROR("The current command buffer implementation does not provide ICommandBufferD3D12 interface."
        );
        return;
    }
    return cmdBuf->ensureInternalDescriptorHeapsBound();
}
#endif

void DebugCommandEncoder::checkEncodersClosedBeforeFinish()
{
    if (!isOpen)
    {
        RHI_VALIDATION_ERROR("Command encoder is already finished.");
    }
    if (m_renderPassEncoder.isOpen)
    {
        RHI_VALIDATION_ERROR(
            "A render pass encoder on this command encoder is still open. "
            "IRenderPassEncoder::end() must be called before finishing a command encoder."
        );
    }
    if (m_computePassEncoder.isOpen)
    {
        RHI_VALIDATION_ERROR(
            "A compute pass encoder on this command encoder is still open. "
            "IComputePassEncoder::end() must be called before finishing a command encoder."
        );
    }
    if (m_resourcePassEncoder.isOpen)
    {
        RHI_VALIDATION_ERROR(
            "A resource pass encoder on this command encoder is still open. "
            "IResourcePassEncoder::end() must be called before finishing a command encoder."
        );
    }
    isOpen = false;
}

void DebugCommandEncoder::checkEncodersClosedBeforeNewEncoder()
{
    if (m_resourcePassEncoder.isOpen || m_renderPassEncoder.isOpen || m_computePassEncoder.isOpen ||
        m_rayTracingPassEncoder.isOpen)
    {
        RHI_VALIDATION_ERROR(
            "A previous pass encoder created on this command encoder is still open. "
            "end() must be called on the pass encoder before creating a new pass encoder."
        );
    }
}

void DebugCommandEncoder::checkCommandBufferOpenWhenCreatingEncoder()
{
    if (!isOpen)
    {
        RHI_VALIDATION_ERROR(
            "The command encoder is already finished. Pass encoders can only be retrieved "
            "while the command encoder is not finished."
        );
    }
}

} // namespace rhi::debug
