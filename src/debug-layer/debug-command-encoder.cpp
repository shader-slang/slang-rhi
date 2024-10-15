#include "debug-command-encoder.h"
#include "debug-command-buffer.h"
#include "debug-helper-functions.h"
#include "debug-pipeline.h"
#include "debug-query.h"

#include <vector>

namespace rhi::debug {

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

DebugCommandEncoder::DebugCommandEncoder(DebugContext* ctx)
    : DebugObject<ICommandEncoder>(ctx)
    , m_rootObject(ctx)
{
}

Result DebugCommandEncoder::preparePipeline(IPipeline* pipeline, IShaderObject** outRootObject)
{
    SLANG_RHI_API_FUNC;

    auto innerPipeline = getInnerObj(pipeline);
    IShaderObject* innerRootObject = nullptr;
    m_rootObject.reset();
    SLANG_RETURN_ON_FAIL(baseObject->preparePipeline(innerPipeline, &innerRootObject));
    m_rootObject.baseObject.attach(innerRootObject);
    *outRootObject = &m_rootObject;
    return SLANG_OK;
}

Result DebugCommandEncoder::preparePipelineWithRootObject(IPipeline* pipeline, IShaderObject* rootObject)
{
    SLANG_RHI_API_FUNC;

    m_rootObject.baseObject = getInnerObj(rootObject);
    SLANG_RETURN_ON_FAIL(baseObject->preparePipelineWithRootObject(getInnerObj(pipeline), getInnerObj(rootObject)));
    return SLANG_OK;
}

Result DebugCommandEncoder::prepareFinish(RenderState* outState)
{
    SLANG_RHI_API_FUNC;
    SLANG_RETURN_ON_FAIL(baseObject->prepareFinish(outState));
    outState->rootObject = &m_rootObject;
    return SLANG_OK;
}

Result DebugCommandEncoder::prepareFinish(ComputeState* outState)
{
    SLANG_RHI_API_FUNC;
    SLANG_RETURN_ON_FAIL(baseObject->prepareFinish(outState));
    outState->rootObject = &m_rootObject;
    return SLANG_OK;
}

Result DebugCommandEncoder::prepareFinish(RayTracingState* outState)
{
    SLANG_RHI_API_FUNC;
    SLANG_RETURN_ON_FAIL(baseObject->prepareFinish(outState));
    outState->rootObject = &m_rootObject;
    return SLANG_OK;
}

void DebugCommandEncoder::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    SLANG_RHI_API_FUNC;
    baseObject->copyBuffer(dst, dstOffset, src, srcOffset, size);
}

void DebugCommandEncoder::uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data)
{
    SLANG_RHI_API_FUNC;
    baseObject->uploadBufferData(dst, offset, size, data);
}

void DebugCommandEncoder::copyTexture(
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
    baseObject->copyTexture(dst, dstSubresource, dstOffset, src, srcSubresource, srcOffset, extent);
}

void DebugCommandEncoder::uploadTextureData(
    ITexture* dst,
    SubresourceRange subresourceRange,
    Offset3D offset,
    Extents extent,
    SubresourceData* subresourceData,
    GfxCount subresourceDataCount
)
{
    SLANG_RHI_API_FUNC;
    baseObject->uploadTextureData(dst, subresourceRange, offset, extent, subresourceData, subresourceDataCount);
}

void DebugCommandEncoder::clearBuffer(IBuffer* buffer, const BufferRange* range)
{
    SLANG_RHI_API_FUNC;
    baseObject->clearBuffer(buffer, range);
}

void DebugCommandEncoder::clearTexture(
    ITexture* texture,
    const ClearValue& clearValue,
    const SubresourceRange* subresourceRange,
    bool clearDepth,
    bool clearStencil
)
{
    SLANG_RHI_API_FUNC;
    baseObject->clearTexture(texture, clearValue, subresourceRange, clearDepth, clearStencil);
}

void DebugCommandEncoder::resolveQuery(
    IQueryPool* queryPool,
    GfxIndex index,
    GfxCount count,
    IBuffer* buffer,
    Offset offset
)
{
    SLANG_RHI_API_FUNC;
    baseObject->resolveQuery(getInnerObj(queryPool), index, count, buffer, offset);
}

void DebugCommandEncoder::copyTextureToBuffer(
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
    baseObject->copyTextureToBuffer(dst, dstOffset, dstSize, dstRowStride, src, srcSubresource, srcOffset, extent);
}

void DebugCommandEncoder::beginRenderPass(const RenderPassDesc& desc)
{
    SLANG_RHI_API_FUNC;
    baseObject->beginRenderPass(desc);
}

void DebugCommandEncoder::endRenderPass()
{
    SLANG_RHI_API_FUNC;
    baseObject->endRenderPass();
}

void DebugCommandEncoder::setRenderState(const RenderState& state)
{
    SLANG_RHI_API_FUNC;
    RenderState innerState = state;
    innerState.rootObject = getInnerObj(state.rootObject);
    baseObject->setRenderState(innerState);
}

void DebugCommandEncoder::draw(const DrawArguments& args)
{
    SLANG_RHI_API_FUNC;
    baseObject->draw(args);
}

void DebugCommandEncoder::drawIndexed(const DrawArguments& args)
{
    SLANG_RHI_API_FUNC;
    baseObject->drawIndexed(args);
}

void DebugCommandEncoder::drawIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    SLANG_RHI_API_FUNC;
    baseObject->drawIndirect(maxDrawCount, argBuffer, argOffset, countBuffer, countOffset);
}

void DebugCommandEncoder::drawIndexedIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    SLANG_RHI_API_FUNC;
    baseObject->drawIndexedIndirect(maxDrawCount, argBuffer, argOffset, countBuffer, countOffset);
}

void DebugCommandEncoder::drawMeshTasks(int x, int y, int z)
{
    SLANG_RHI_API_FUNC;
    baseObject->drawMeshTasks(x, y, z);
}

void DebugCommandEncoder::setComputeState(const ComputeState& state)
{
    SLANG_RHI_API_FUNC;
    ComputeState innerState = state;
    innerState.rootObject = getInnerObj(state.rootObject);
    baseObject->setComputeState(innerState);
}

void DebugCommandEncoder::dispatchCompute(int x, int y, int z)
{
    SLANG_RHI_API_FUNC;
    baseObject->dispatchCompute(x, y, z);
}

void DebugCommandEncoder::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    SLANG_RHI_API_FUNC;
    baseObject->dispatchComputeIndirect(argBuffer, offset);
}

void DebugCommandEncoder::setRayTracingState(const RayTracingState& state)
{
    SLANG_RHI_API_FUNC;
    RayTracingState innerState = state;
    innerState.rootObject = getInnerObj(state.rootObject);
    baseObject->setRayTracingState(innerState);
}

/// Issues a dispatch command to start ray tracing workload with a ray tracing pipeline.
/// `rayGenShaderIndex` specifies the index into the shader table that identifies the ray generation shader.
void DebugCommandEncoder::dispatchRays(GfxIndex rayGenShaderIndex, GfxCount width, GfxCount height, GfxCount depth)
{
    SLANG_RHI_API_FUNC;
    baseObject->dispatchRays(rayGenShaderIndex, width, height, depth);
}

void DebugCommandEncoder::buildAccelerationStructure(
    const AccelerationStructureBuildDesc& desc,
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    BufferWithOffset scratchBuffer,
    GfxCount propertyQueryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    SLANG_RHI_API_FUNC;
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
    baseObject->buildAccelerationStructure(desc, dst, src, scratchBuffer, propertyQueryCount, innerQueryDescs.data());
}

void DebugCommandEncoder::copyAccelerationStructure(
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    AccelerationStructureCopyMode mode
)
{
    SLANG_RHI_API_FUNC;
    baseObject->copyAccelerationStructure(dst, src, mode);
}

void DebugCommandEncoder::queryAccelerationStructureProperties(
    GfxCount accelerationStructureCount,
    IAccelerationStructure* const* accelerationStructures,
    GfxCount queryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    SLANG_RHI_API_FUNC;
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
        accelerationStructures,
        queryCount,
        innerQueryDescs.data()
    );
}

void DebugCommandEncoder::serializeAccelerationStructure(BufferWithOffset dst, IAccelerationStructure* src)
{
    SLANG_RHI_API_FUNC;
    baseObject->serializeAccelerationStructure(dst, src);
}

void DebugCommandEncoder::deserializeAccelerationStructure(IAccelerationStructure* dst, BufferWithOffset src)
{
    SLANG_RHI_API_FUNC;
    baseObject->deserializeAccelerationStructure(dst, src);
}

void DebugCommandEncoder::setBufferState(IBuffer* buffer, ResourceState state)
{
    SLANG_RHI_API_FUNC;
    baseObject->setBufferState(buffer, state);
}

void DebugCommandEncoder::setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state)
{
    SLANG_RHI_API_FUNC;
    baseObject->setTextureState(texture, subresourceRange, state);
}

void DebugCommandEncoder::beginDebugEvent(const char* name, float rgbColor[3])
{
    SLANG_RHI_API_FUNC;
    baseObject->beginDebugEvent(name, rgbColor);
}

void DebugCommandEncoder::endDebugEvent()
{
    SLANG_RHI_API_FUNC;
    baseObject->endDebugEvent();
}

void DebugCommandEncoder::writeTimestamp(IQueryPool* pool, GfxIndex index)
{
    SLANG_RHI_API_FUNC;
    baseObject->writeTimestamp(getInnerObj(pool), index);
}

Result DebugCommandEncoder::finish(ICommandBuffer** outCommandBuffer)
{
    SLANG_RHI_API_FUNC;
#if 0
    checkEncodersClosedBeforeFinish();
#endif
    RefPtr<DebugCommandBuffer> outObject = new DebugCommandBuffer(ctx);
    auto result = baseObject->finish(outObject->baseObject.writeRef());
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
#endif

} // namespace rhi::debug
