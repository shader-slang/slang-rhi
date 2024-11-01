#include "debug-command-encoder.h"
#include "debug-command-buffer.h"
#include "debug-helper-functions.h"
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
{
}

void DebugCommandEncoder::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    baseObject->copyBuffer(dst, dstOffset, src, srcOffset, size);
}

void DebugCommandEncoder::uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
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
    requireOpen();
    requireNoPass();
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
    requireOpen();
    requireNoPass();
    baseObject->uploadTextureData(dst, subresourceRange, offset, extent, subresourceData, subresourceDataCount);
}

void DebugCommandEncoder::clearBuffer(IBuffer* buffer, const BufferRange* range)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
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
    requireOpen();
    requireNoPass();
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
    requireOpen();
    requireNoPass();
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
    requireOpen();
    requireNoPass();
    baseObject->copyTextureToBuffer(dst, dstOffset, dstSize, dstRowStride, src, srcSubresource, srcOffset, extent);
}

void DebugCommandEncoder::beginRenderPass(const RenderPassDesc& desc)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    m_passState = PassState::RenderPass;
    baseObject->beginRenderPass(desc);
}

void DebugCommandEncoder::endRenderPass()
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireRenderPass();
    m_passState = PassState::NoPass;
    baseObject->endRenderPass();
}

void DebugCommandEncoder::setRenderState(const RenderState& state)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireRenderPass();
    if (!state.rootObject->isFinalized())
    {
        RHI_VALIDATION_ERROR("The render state root object must be finalized.");
    }
    RenderState innerState = state;
    innerState.rootObject = getInnerObj(state.rootObject);
    baseObject->setRenderState(innerState);
}

void DebugCommandEncoder::draw(const DrawArguments& args)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireRenderPass();
    baseObject->draw(args);
}

void DebugCommandEncoder::drawIndexed(const DrawArguments& args)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireRenderPass();
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
    requireOpen();
    requireRenderPass();
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
    requireOpen();
    requireRenderPass();
    baseObject->drawIndexedIndirect(maxDrawCount, argBuffer, argOffset, countBuffer, countOffset);
}

void DebugCommandEncoder::drawMeshTasks(int x, int y, int z)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireRenderPass();
    baseObject->drawMeshTasks(x, y, z);
}

void DebugCommandEncoder::beginComputePass()
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    m_passState = PassState::ComputePass;
    baseObject->beginComputePass();
}

void DebugCommandEncoder::endComputePass()
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireComputePass();
    m_passState = PassState::NoPass;
    baseObject->endComputePass();
}

void DebugCommandEncoder::setComputeState(const ComputeState& state)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireComputePass();
    if (!state.rootObject->isFinalized())
    {
        RHI_VALIDATION_ERROR("The compute state root object must be finalized.");
    }
    ComputeState innerState = state;
    innerState.rootObject = getInnerObj(state.rootObject);
    baseObject->setComputeState(innerState);
}

void DebugCommandEncoder::dispatchCompute(int x, int y, int z)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireComputePass();
    baseObject->dispatchCompute(x, y, z);
}

void DebugCommandEncoder::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireComputePass();
    baseObject->dispatchComputeIndirect(argBuffer, offset);
}

void DebugCommandEncoder::beginRayTracingPass()
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    m_passState = PassState::RayTracingPass;
    baseObject->beginRayTracingPass();
}

void DebugCommandEncoder::endRayTracingPass()
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireRayTracingPass();
    m_passState = PassState::NoPass;
    baseObject->endRayTracingPass();
}

void DebugCommandEncoder::setRayTracingState(const RayTracingState& state)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireRayTracingPass();
    if (!state.rootObject->isFinalized())
    {
        RHI_VALIDATION_ERROR("The raytracing state root object must be finalized.");
    }
    RayTracingState innerState = state;
    innerState.rootObject = getInnerObj(state.rootObject);
    baseObject->setRayTracingState(innerState);
}

/// Issues a dispatch command to start ray tracing workload with a ray tracing pipeline.
/// `rayGenShaderIndex` specifies the index into the shader table that identifies the ray generation shader.
void DebugCommandEncoder::dispatchRays(GfxIndex rayGenShaderIndex, GfxCount width, GfxCount height, GfxCount depth)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireRayTracingPass();
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
    requireOpen();
    requireNoPass();
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
    requireOpen();
    requireNoPass();
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
    requireOpen();
    requireNoPass();
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
    requireOpen();
    requireNoPass();
    baseObject->serializeAccelerationStructure(dst, src);
}

void DebugCommandEncoder::deserializeAccelerationStructure(IAccelerationStructure* dst, BufferWithOffset src)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    baseObject->deserializeAccelerationStructure(dst, src);
}

void DebugCommandEncoder::setBufferState(IBuffer* buffer, ResourceState state)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    baseObject->setBufferState(buffer, state);
}

void DebugCommandEncoder::setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    baseObject->setTextureState(texture, subresourceRange, state);
}

void DebugCommandEncoder::beginDebugEvent(const char* name, float rgbColor[3])
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    baseObject->beginDebugEvent(name, rgbColor);
}

void DebugCommandEncoder::endDebugEvent()
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    baseObject->endDebugEvent();
}

void DebugCommandEncoder::writeTimestamp(IQueryPool* pool, GfxIndex index)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    baseObject->writeTimestamp(getInnerObj(pool), index);
}

Result DebugCommandEncoder::finish(ICommandBuffer** outCommandBuffer)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
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

void DebugCommandEncoder::requireOpen()
{
    if (m_state != EncoderState::Open)
    {
        RHI_VALIDATION_ERROR("The command encoder must not be finished.");
    }
}

void DebugCommandEncoder::requireNoPass()
{
    if (m_passState != PassState::NoPass)
    {
        RHI_VALIDATION_ERROR("The command encoder must not be in a render, compute or ray-tracing pass.");
    }
}

void DebugCommandEncoder::requireRenderPass()
{
    if (m_passState != PassState::RenderPass)
    {
        RHI_VALIDATION_ERROR("The command encoder must be in a render pass.");
    }
}

void DebugCommandEncoder::requireComputePass()
{
    if (m_passState != PassState::ComputePass)
    {
        RHI_VALIDATION_ERROR("The command encoder must be in a compute pass.");
    }
}

void DebugCommandEncoder::requireRayTracingPass()
{
    if (m_passState != PassState::RayTracingPass)
    {
        RHI_VALIDATION_ERROR("The command encoder must be in a ray-tracing pass.");
    }
}

} // namespace rhi::debug
