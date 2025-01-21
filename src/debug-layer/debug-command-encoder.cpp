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

DebugRenderPassEncoder::DebugRenderPassEncoder(DebugContext* ctx, DebugCommandEncoder* commandEncoder)
    : UnownedDebugObject<IRenderPassEncoder>(ctx)
    , m_commandEncoder(commandEncoder)
{
}

void DebugRenderPassEncoder::setRenderState(const RenderState& state)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    if (!state.rootObject->isFinalized())
    {
        RHI_VALIDATION_ERROR("The render state root object must be finalized.");
    }
    RenderState innerState = state;
    innerState.rootObject = getInnerObj(state.rootObject);
    baseObject->setRenderState(innerState);
}

void DebugRenderPassEncoder::draw(const DrawArguments& args)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    baseObject->draw(args);
}

void DebugRenderPassEncoder::drawIndexed(const DrawArguments& args)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    baseObject->drawIndexed(args);
}

void DebugRenderPassEncoder::drawIndirect(
    uint32_t maxDrawCount,
    IBuffer* argBuffer,
    uint64_t argOffset,
    IBuffer* countBuffer,
    uint64_t countOffset
)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    baseObject->drawIndirect(maxDrawCount, argBuffer, argOffset, countBuffer, countOffset);
}

void DebugRenderPassEncoder::drawIndexedIndirect(
    uint32_t maxDrawCount,
    IBuffer* argBuffer,
    uint64_t argOffset,
    IBuffer* countBuffer,
    uint64_t countOffset
)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    baseObject->drawIndexedIndirect(maxDrawCount, argBuffer, argOffset, countBuffer, countOffset);
}

void DebugRenderPassEncoder::drawMeshTasks(uint32_t x, uint32_t y, uint32_t z)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    baseObject->drawMeshTasks(x, y, z);
}

void DebugRenderPassEncoder::pushDebugGroup(const char* name, float rgbColor[3])
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    baseObject->pushDebugGroup(name, rgbColor);
}

void DebugRenderPassEncoder::popDebugGroup()
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    baseObject->popDebugGroup();
}

void DebugRenderPassEncoder::insertDebugMarker(const char* name, float rgbColor[3])
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    baseObject->insertDebugMarker(name, rgbColor);
}

void DebugRenderPassEncoder::end()
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    m_commandEncoder->m_passState = DebugCommandEncoder::PassState::NoPass;
    baseObject->end();
}

DebugComputePassEncoder::DebugComputePassEncoder(DebugContext* ctx, DebugCommandEncoder* commandEncoder)
    : UnownedDebugObject<IComputePassEncoder>(ctx)
    , m_commandEncoder(commandEncoder)
{
}

void DebugComputePassEncoder::setComputeState(const ComputeState& state)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();
    if (!state.rootObject->isFinalized())
    {
        RHI_VALIDATION_ERROR("The compute state root object must be finalized.");
    }
    ComputeState innerState = state;
    innerState.rootObject = getInnerObj(state.rootObject);
    baseObject->setComputeState(innerState);
}

void DebugComputePassEncoder::dispatchCompute(GfxCount x, GfxCount y, GfxCount z)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();
    baseObject->dispatchCompute(x, y, z);
}

void DebugComputePassEncoder::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();
    baseObject->dispatchComputeIndirect(argBuffer, offset);
}

void DebugComputePassEncoder::pushDebugGroup(const char* name, float rgbColor[3])
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();
    baseObject->pushDebugGroup(name, rgbColor);
}

void DebugComputePassEncoder::popDebugGroup()
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();
    baseObject->popDebugGroup();
}

void DebugComputePassEncoder::insertDebugMarker(const char* name, float rgbColor[3])
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();
    baseObject->insertDebugMarker(name, rgbColor);
}

void DebugComputePassEncoder::end()
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();
    m_commandEncoder->m_passState = DebugCommandEncoder::PassState::NoPass;
    baseObject->end();
}

DebugRayTracingPassEncoder::DebugRayTracingPassEncoder(DebugContext* ctx, DebugCommandEncoder* commandEncoder)
    : UnownedDebugObject<IRayTracingPassEncoder>(ctx)
    , m_commandEncoder(commandEncoder)
{
}

void DebugRayTracingPassEncoder::setRayTracingState(const RayTracingState& state)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();
    if (!state.rootObject->isFinalized())
    {
        RHI_VALIDATION_ERROR("The raytracing state root object must be finalized.");
    }
    RayTracingState innerState = state;
    innerState.rootObject = getInnerObj(state.rootObject);
    baseObject->setRayTracingState(innerState);
}

void DebugRayTracingPassEncoder::dispatchRays(
    GfxIndex rayGenShaderIndex,
    GfxCount width,
    GfxCount height,
    GfxCount depth
)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();
    baseObject->dispatchRays(rayGenShaderIndex, width, height, depth);
}

void DebugRayTracingPassEncoder::pushDebugGroup(const char* name, float rgbColor[3])
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();
    baseObject->pushDebugGroup(name, rgbColor);
}

void DebugRayTracingPassEncoder::popDebugGroup()
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();
    baseObject->popDebugGroup();
}

void DebugRayTracingPassEncoder::insertDebugMarker(const char* name, float rgbColor[3])
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();
    baseObject->insertDebugMarker(name, rgbColor);
}

void DebugRayTracingPassEncoder::end()
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();
    m_commandEncoder->m_passState = DebugCommandEncoder::PassState::NoPass;
    baseObject->end();
}

DebugCommandEncoder::DebugCommandEncoder(DebugContext* ctx)
    : DebugObject<ICommandEncoder>(ctx)
    , m_renderPassEncoder(ctx, this)
    , m_computePassEncoder(ctx, this)
    , m_rayTracingPassEncoder(ctx, this)
{
}

IRenderPassEncoder* DebugCommandEncoder::beginRenderPass(const RenderPassDesc& desc)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    m_passState = PassState::RenderPass;
    m_renderPassEncoder.baseObject = baseObject->beginRenderPass(desc);
    return &m_renderPassEncoder;
}

IComputePassEncoder* DebugCommandEncoder::beginComputePass()
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    m_passState = PassState::ComputePass;
    m_computePassEncoder.baseObject = baseObject->beginComputePass();
    return &m_computePassEncoder;
}

IRayTracingPassEncoder* DebugCommandEncoder::beginRayTracingPass()
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    m_passState = PassState::RayTracingPass;
    m_rayTracingPassEncoder.baseObject = baseObject->beginRayTracingPass();
    return &m_rayTracingPassEncoder;
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
    requireNoPass();
    baseObject->setBufferState(buffer, state);
}

void DebugCommandEncoder::setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    baseObject->setTextureState(texture, subresourceRange, state);
}

void DebugCommandEncoder::pushDebugGroup(const char* name, float rgbColor[3])
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    baseObject->pushDebugGroup(name, rgbColor);
}

void DebugCommandEncoder::popDebugGroup()
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    baseObject->popDebugGroup();
}

void DebugCommandEncoder::insertDebugMarker(const char* name, float rgbColor[3])
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    baseObject->insertDebugMarker(name, rgbColor);
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
