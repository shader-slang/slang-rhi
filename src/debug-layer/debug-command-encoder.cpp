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

// DebugCommandEncoder

void DebugCommandEncoder::textureBarrier(
    GfxCount count,
    ITexture* const* textures,
    ResourceState src,
    ResourceState dst
)
{
    SLANG_RHI_API_FUNC;
    std::vector<ITexture*> innerTextures;
    for (GfxIndex i = 0; i < count; i++)
    {
        innerTextures.push_back(static_cast<DebugTexture*>(textures[i])->baseObject.get());
    }
    getBaseObject()->textureBarrier(count, innerTextures.data(), src, dst);
}

void DebugCommandEncoder::textureSubresourceBarrier(
    ITexture* texture,
    SubresourceRange subresourceRange,
    ResourceState src,
    ResourceState dst
)
{
    SLANG_RHI_API_FUNC;
    getBaseObject()->textureSubresourceBarrier(getInnerObj(texture), subresourceRange, src, dst);
}

void DebugCommandEncoder::bufferBarrier(GfxCount count, IBuffer* const* buffers, ResourceState src, ResourceState dst)
{
    SLANG_RHI_API_FUNC;
    std::vector<IBuffer*> innerBuffers;
    for (GfxIndex i = 0; i < count; i++)
    {
        innerBuffers.push_back(static_cast<DebugBuffer*>(buffers[i])->baseObject.get());
    }
    getBaseObject()->bufferBarrier(count, innerBuffers.data(), src, dst);
}

void DebugCommandEncoder::beginDebugEvent(const char* name, float rgbColor[3])
{
    SLANG_RHI_API_FUNC;
    getBaseObject()->beginDebugEvent(name, rgbColor);
}

void DebugCommandEncoder::endDebugEvent()
{
    SLANG_RHI_API_FUNC;
    getBaseObject()->endDebugEvent();
}

void DebugCommandEncoder::writeTimestamp(IQueryPool* pool, GfxIndex index)
{
    getBaseObject()->writeTimestamp(getInnerObj(pool), index);
}

// DebugResourceCommandEncoder

void DebugResourceCommandEncoder::endEncoding()
{
    SLANG_RHI_API_FUNC;
    isOpen = false;
    baseObject->endEncoding();
}

void DebugResourceCommandEncoder::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    SLANG_RHI_API_FUNC;
    auto dstImpl = static_cast<DebugBuffer*>(dst);
    auto srcImpl = static_cast<DebugBuffer*>(src);
    baseObject->copyBuffer(dstImpl->baseObject, dstOffset, srcImpl->baseObject, srcOffset, size);
}

void DebugResourceCommandEncoder::uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data)
{
    SLANG_RHI_API_FUNC;
    auto dstImpl = static_cast<DebugBuffer*>(dst);
    baseObject->uploadBufferData(dstImpl->baseObject, offset, size, data);
}

void DebugResourceCommandEncoder::copyTexture(
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
    SLANG_RHI_API_FUNC;
    baseObject->copyTexture(
        getInnerObj(dst),
        dstState,
        dstSubresource,
        dstOffset,
        getInnerObj(src),
        srcState,
        srcSubresource,
        srcOffset,
        extent
    );
}

void DebugResourceCommandEncoder::uploadTextureData(
    ITexture* dst,
    SubresourceRange subResourceRange,
    Offset3D offset,
    Extents extent,
    SubresourceData* subResourceData,
    GfxCount subResourceDataCount
)
{
    SLANG_RHI_API_FUNC;
    baseObject
        ->uploadTextureData(getInnerObj(dst), subResourceRange, offset, extent, subResourceData, subResourceDataCount);
}

void DebugResourceCommandEncoder::clearBuffer(IBuffer* buffer, const BufferRange* range)
{
    SLANG_RHI_API_FUNC;
    baseObject->clearBuffer(getInnerObj(buffer), range);
}

void DebugResourceCommandEncoder::clearTexture(
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

void DebugResourceCommandEncoder::resolveResource(
    ITexture* source,
    ResourceState sourceState,
    SubresourceRange sourceRange,
    ITexture* dest,
    ResourceState destState,
    SubresourceRange destRange
)
{
    SLANG_RHI_API_FUNC;
    baseObject->resolveResource(getInnerObj(source), sourceState, sourceRange, getInnerObj(dest), destState, destRange);
}

void DebugResourceCommandEncoder::resolveQuery(
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

void DebugResourceCommandEncoder::copyTextureToBuffer(
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
    SLANG_RHI_API_FUNC;
    baseObject->copyTextureToBuffer(
        getInnerObj(dst),
        dstOffset,
        dstSize,
        dstRowStride,
        getInnerObj(src),
        srcState,
        srcSubresource,
        srcOffset,
        extent
    );
}

// DebugRenderCommandEncoder

void DebugRenderCommandEncoder::endEncoding()
{
    SLANG_RHI_API_FUNC;
    isOpen = false;
    baseObject->endEncoding();
}

Result DebugRenderCommandEncoder::bindPipeline(IPipeline* state, IShaderObject** outRootShaderObject)
{
    SLANG_RHI_API_FUNC;

    auto innerState = getInnerObj(state);
    IShaderObject* innerRootObject = nullptr;
    commandBuffer->rootObject.reset();
    auto result = baseObject->bindPipeline(innerState, &innerRootObject);
    commandBuffer->rootObject.baseObject.attach(innerRootObject);
    *outRootShaderObject = &commandBuffer->rootObject;
    return result;
}

Result DebugRenderCommandEncoder::bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject)
{
    SLANG_RHI_API_FUNC;
    return baseObject->bindPipelineWithRootObject(getInnerObj(state), getInnerObj(rootObject));
}

void DebugRenderCommandEncoder::setViewports(GfxCount count, const Viewport* viewports)
{
    SLANG_RHI_API_FUNC;
    baseObject->setViewports(count, viewports);
}

void DebugRenderCommandEncoder::setScissorRects(GfxCount count, const ScissorRect* scissors)
{
    SLANG_RHI_API_FUNC;
    baseObject->setScissorRects(count, scissors);
}

void DebugRenderCommandEncoder::setPrimitiveTopology(PrimitiveTopology topology)
{
    SLANG_RHI_API_FUNC;
    baseObject->setPrimitiveTopology(topology);
}

void DebugRenderCommandEncoder::setVertexBuffers(
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
        innerBuffers.push_back(static_cast<DebugBuffer*>(buffers[i])->baseObject.get());
    }
    baseObject->setVertexBuffers(startSlot, slotCount, innerBuffers.data(), offsets);
}

void DebugRenderCommandEncoder::setIndexBuffer(IBuffer* buffer, Format indexFormat, Offset offset)
{
    SLANG_RHI_API_FUNC;
    auto innerBuffer = static_cast<DebugBuffer*>(buffer)->baseObject.get();
    baseObject->setIndexBuffer(innerBuffer, indexFormat, offset);
}

Result DebugRenderCommandEncoder::draw(GfxCount vertexCount, GfxIndex startVertex)
{
    SLANG_RHI_API_FUNC;
    return baseObject->draw(vertexCount, startVertex);
}

Result DebugRenderCommandEncoder::drawIndexed(GfxCount indexCount, GfxIndex startIndex, GfxIndex baseVertex)
{
    SLANG_RHI_API_FUNC;
    return baseObject->drawIndexed(indexCount, startIndex, baseVertex);
}

Result DebugRenderCommandEncoder::drawIndirect(
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

Result DebugRenderCommandEncoder::drawIndexedIndirect(
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

void DebugRenderCommandEncoder::setStencilReference(uint32_t referenceValue)
{
    SLANG_RHI_API_FUNC;
    return baseObject->setStencilReference(referenceValue);
}

Result DebugRenderCommandEncoder::setSamplePositions(
    GfxCount samplesPerPixel,
    GfxCount pixelCount,
    const SamplePosition* samplePositions
)
{
    SLANG_RHI_API_FUNC;
    return baseObject->setSamplePositions(samplesPerPixel, pixelCount, samplePositions);
}

Result DebugRenderCommandEncoder::drawInstanced(
    GfxCount vertexCount,
    GfxCount instanceCount,
    GfxIndex startVertex,
    GfxIndex startInstanceLocation
)
{
    SLANG_RHI_API_FUNC;
    return baseObject->drawInstanced(vertexCount, instanceCount, startVertex, startInstanceLocation);
}

Result DebugRenderCommandEncoder::drawIndexedInstanced(
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

Result DebugRenderCommandEncoder::drawMeshTasks(int x, int y, int z)
{
    SLANG_RHI_API_FUNC;
    return baseObject->drawMeshTasks(x, y, z);
}

// DebugComputeCommandEncoder

void DebugComputeCommandEncoder::endEncoding()
{
    SLANG_RHI_API_FUNC;
    isOpen = false;
    baseObject->endEncoding();
}

Result DebugComputeCommandEncoder::bindPipeline(IPipeline* state, IShaderObject** outRootShaderObject)
{
    SLANG_RHI_API_FUNC;

    auto innerState = getInnerObj(state);
    IShaderObject* innerRootObject = nullptr;
    commandBuffer->rootObject.reset();
    auto result = baseObject->bindPipeline(innerState, &innerRootObject);
    commandBuffer->rootObject.baseObject.attach(innerRootObject);
    *outRootShaderObject = &commandBuffer->rootObject;
    return result;
}

Result DebugComputeCommandEncoder::bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject)
{
    SLANG_RHI_API_FUNC;
    return baseObject->bindPipelineWithRootObject(getInnerObj(state), getInnerObj(rootObject));
}

Result DebugComputeCommandEncoder::dispatchCompute(int x, int y, int z)
{
    SLANG_RHI_API_FUNC;
    return baseObject->dispatchCompute(x, y, z);
}

Result DebugComputeCommandEncoder::dispatchComputeIndirect(IBuffer* cmdBuffer, Offset offset)
{
    SLANG_RHI_API_FUNC;
    return baseObject->dispatchComputeIndirect(getInnerObj(cmdBuffer), offset);
}

// DebugRayTracingCommandEncoder

void DebugRayTracingCommandEncoder::endEncoding()
{
    SLANG_RHI_API_FUNC;
    isOpen = false;
    baseObject->endEncoding();
}

void DebugRayTracingCommandEncoder::buildAccelerationStructure(
    const IAccelerationStructure::BuildDesc& desc,
    GfxCount propertyQueryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    SLANG_RHI_API_FUNC;
    IAccelerationStructure::BuildDesc innerDesc = desc;
    innerDesc.dest = getInnerObj(innerDesc.dest);
    innerDesc.source = getInnerObj(innerDesc.source);
    std::vector<AccelerationStructureQueryDesc> innerQueryDescs;
    for (size_t i = 0; i < propertyQueryCount; ++i)
    {
        innerQueryDescs.push_back(queryDescs[i]);
    }
    for (auto& innerQueryDesc : innerQueryDescs)
    {
        innerQueryDesc.queryPool = getInnerObj(innerQueryDesc.queryPool);
    }
    validateAccelerationStructureBuildInputs(desc.inputs);
    baseObject->buildAccelerationStructure(innerDesc, propertyQueryCount, innerQueryDescs.data());
}

void DebugRayTracingCommandEncoder::copyAccelerationStructure(
    IAccelerationStructure* dest,
    IAccelerationStructure* src,
    AccelerationStructureCopyMode mode
)
{
    SLANG_RHI_API_FUNC;
    auto innerDest = getInnerObj(dest);
    auto innerSrc = getInnerObj(src);
    baseObject->copyAccelerationStructure(innerDest, innerSrc, mode);
}

void DebugRayTracingCommandEncoder::queryAccelerationStructureProperties(
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

void DebugRayTracingCommandEncoder::serializeAccelerationStructure(DeviceAddress dest, IAccelerationStructure* source)
{
    SLANG_RHI_API_FUNC;
    baseObject->serializeAccelerationStructure(dest, getInnerObj(source));
}

void DebugRayTracingCommandEncoder::deserializeAccelerationStructure(IAccelerationStructure* dest, DeviceAddress source)
{
    SLANG_RHI_API_FUNC;
    baseObject->deserializeAccelerationStructure(getInnerObj(dest), source);
}

Result DebugRayTracingCommandEncoder::bindPipeline(IPipeline* state, IShaderObject** outRootObject)
{
    SLANG_RHI_API_FUNC;
    auto innerPipeline = getInnerObj(state);
    IShaderObject* innerRootObject = nullptr;
    commandBuffer->rootObject.reset();
    Result result = baseObject->bindPipeline(innerPipeline, &innerRootObject);
    commandBuffer->rootObject.baseObject.attach(innerRootObject);
    *outRootObject = &commandBuffer->rootObject;
    return result;
}

Result DebugRayTracingCommandEncoder::bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject)
{
    SLANG_RHI_API_FUNC;
    return baseObject->bindPipelineWithRootObject(getInnerObj(state), getInnerObj(rootObject));
}

Result DebugRayTracingCommandEncoder::dispatchRays(
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

} // namespace rhi::debug
