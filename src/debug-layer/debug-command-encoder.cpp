#include "debug-command-encoder.h"
#include "debug-command-buffer.h"
#include "debug-helper-functions.h"
#include "debug-query.h"

#include <vector>

namespace rhi::debug {

DebugRenderPassEncoder::DebugRenderPassEncoder(DebugContext* ctx, DebugCommandEncoder* commandEncoder)
    : UnownedDebugObject<IRenderPassEncoder>(ctx)
    , m_commandEncoder(commandEncoder)
{
    m_rootObject = new DebugRootShaderObject(ctx);
}

IShaderObject* DebugRenderPassEncoder::bindPipeline(IRenderPipeline* pipeline)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    m_rootObject->reset();
    m_rootObject->baseObject = baseObject->bindPipeline(pipeline);
    return m_rootObject;
}

void DebugRenderPassEncoder::bindPipeline(IRenderPipeline* pipeline, IShaderObject* rootObject)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    baseObject->bindPipeline(pipeline, getInnerObj(rootObject));
}

void DebugRenderPassEncoder::setRenderState(const RenderState& state)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    baseObject->setRenderState(state);
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
    BufferOffsetPair argBuffer,
    BufferOffsetPair countBuffer
)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    baseObject->drawIndirect(maxDrawCount, argBuffer, countBuffer);
}

void DebugRenderPassEncoder::drawIndexedIndirect(
    uint32_t maxDrawCount,
    BufferOffsetPair argBuffer,
    BufferOffsetPair countBuffer
)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    baseObject->drawIndexedIndirect(maxDrawCount, argBuffer, countBuffer);
}

void DebugRenderPassEncoder::drawMeshTasks(uint32_t x, uint32_t y, uint32_t z)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    baseObject->drawMeshTasks(x, y, z);
}

void DebugRenderPassEncoder::pushDebugGroup(const char* name, const MarkerColor& color)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    baseObject->pushDebugGroup(name, color);
}

void DebugRenderPassEncoder::popDebugGroup()
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    baseObject->popDebugGroup();
}

void DebugRenderPassEncoder::insertDebugMarker(const char* name, const MarkerColor& color)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    baseObject->insertDebugMarker(name, color);
}

void DebugRenderPassEncoder::writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    baseObject->writeTimestamp(getInnerObj(queryPool), queryIndex);
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
    m_rootObject = new DebugRootShaderObject(ctx);
}

IShaderObject* DebugComputePassEncoder::bindPipeline(IComputePipeline* pipeline)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();
    m_rootObject->reset();
    m_rootObject->baseObject = baseObject->bindPipeline(pipeline);
    return m_rootObject;
}

void DebugComputePassEncoder::bindPipeline(IComputePipeline* pipeline, IShaderObject* rootObject)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();
    baseObject->bindPipeline(pipeline, getInnerObj(rootObject));
}

void DebugComputePassEncoder::dispatchCompute(uint32_t x, uint32_t y, uint32_t z)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();
    baseObject->dispatchCompute(x, y, z);
}

void DebugComputePassEncoder::dispatchComputeIndirect(BufferOffsetPair argBuffer)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();
    baseObject->dispatchComputeIndirect(argBuffer);
}

void DebugComputePassEncoder::pushDebugGroup(const char* name, const MarkerColor& color)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();
    baseObject->pushDebugGroup(name, color);
}

void DebugComputePassEncoder::popDebugGroup()
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();
    baseObject->popDebugGroup();
}

void DebugComputePassEncoder::insertDebugMarker(const char* name, const MarkerColor& color)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();
    baseObject->insertDebugMarker(name, color);
}

void DebugComputePassEncoder::writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();
    baseObject->writeTimestamp(getInnerObj(queryPool), queryIndex);
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
    m_rootObject = new DebugRootShaderObject(ctx);
}

IShaderObject* DebugRayTracingPassEncoder::bindPipeline(IRayTracingPipeline* pipeline, IShaderTable* shaderTable)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();
    m_rootObject->reset();
    m_rootObject->baseObject = baseObject->bindPipeline(pipeline, shaderTable);
    return m_rootObject;
}

void DebugRayTracingPassEncoder::bindPipeline(
    IRayTracingPipeline* pipeline,
    IShaderTable* shaderTable,
    IShaderObject* rootObject
)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();
    baseObject->bindPipeline(pipeline, shaderTable, getInnerObj(rootObject));
}

void DebugRayTracingPassEncoder::dispatchRays(
    uint32_t rayGenShaderIndex,
    uint32_t width,
    uint32_t height,
    uint32_t depth
)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();
    baseObject->dispatchRays(rayGenShaderIndex, width, height, depth);
}

void DebugRayTracingPassEncoder::pushDebugGroup(const char* name, const MarkerColor& color)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();
    baseObject->pushDebugGroup(name, color);
}

void DebugRayTracingPassEncoder::popDebugGroup()
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();
    baseObject->popDebugGroup();
}

void DebugRayTracingPassEncoder::insertDebugMarker(const char* name, const MarkerColor& color)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();
    baseObject->insertDebugMarker(name, color);
}

void DebugRayTracingPassEncoder::writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex)
{
    SLANG_RHI_API_FUNC;
    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();
    baseObject->writeTimestamp(getInnerObj(queryPool), queryIndex);
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

Result DebugCommandEncoder::uploadBufferData(IBuffer* dst, Offset offset, Size size, const void* data)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    return baseObject->uploadBufferData(dst, offset, size, data);
}

void DebugCommandEncoder::copyTexture(
    ITexture* dst,
    SubresourceRange dstSubresource,
    Offset3D dstOffset,
    ITexture* src,
    SubresourceRange srcSubresource,
    Offset3D srcOffset,
    Extent3D extent
)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();

    const TextureDesc& srcDesc = src->getDesc();
    if (srcSubresource.layer > srcDesc.getLayerCount())
    {
        RHI_VALIDATION_ERROR("Src layer is out of bounds.");
        return;
    }
    if (srcSubresource.mip > srcDesc.mipCount)
    {
        RHI_VALIDATION_ERROR("Src mip is out of bounds.");
        return;
    }

    const TextureDesc& dstDesc = dst->getDesc();
    if (dstSubresource.layer > dstDesc.getLayerCount())
    {
        RHI_VALIDATION_ERROR("Dest layer is out of bounds.");
        return;
    }
    if (dstSubresource.mip > dstDesc.mipCount)
    {
        RHI_VALIDATION_ERROR("Dest mip is out of bounds.");
        return;
    }

    if (srcSubresource.layerCount != dstSubresource.layerCount)
    {
        RHI_VALIDATION_ERROR("Src and dest layer count must match.");
        return;
    }

    if (srcSubresource.mipCount != dstSubresource.mipCount)
    {
        RHI_VALIDATION_ERROR("Src and dest mip count must match.");
        return;
    }

    if (srcSubresource.layerCount == 0 && srcDesc.getLayerCount() != dstDesc.getLayerCount())
    {
        RHI_VALIDATION_ERROR("Copy layer count is 0, so src and dest texture layer count must match.");
        return;
    }

    if (srcSubresource.mipCount == 0 && srcDesc.mipCount != dstDesc.mipCount)
    {
        RHI_VALIDATION_ERROR("Copy mip count is 0, so src and dest texture mip count must match.");
        return;
    }

    if (srcSubresource.mipCount != 1)
    {
        if (!srcOffset.isZero() || !dstOffset.isZero())
        {
            RHI_VALIDATION_ERROR("Copying multiple mip levels at once requires offset to be 0");
            return;
        }

        if (!extent.isWholeTexture())
        {
            RHI_VALIDATION_ERROR("Copying multiple mip levels at once requires extent to be Extent3D::kWholeTexture");
            return;
        }
    }

    if (extent.width == kRemainingTextureSize)
    {
        if (srcOffset.x != dstOffset.x)
        {
            RHI_VALIDATION_ERROR("Copying the remaining texture requires src and dst offset to be the same");
            return;
        }
    }
    if (extent.height == kRemainingTextureSize)
    {
        if (srcOffset.y != dstOffset.y)
        {
            RHI_VALIDATION_ERROR("Copying the remaining texture requires src and dst offset to be the same");
            return;
        }
    }
    if (extent.depth == kRemainingTextureSize)
    {
        if (srcOffset.z != dstOffset.z)
        {
            RHI_VALIDATION_ERROR("Copying the remaining texture requires src and dst offset to be the same");
            return;
        }
    }

    if ((srcDesc.type == TextureType::Texture3D) ^ (dstDesc.type == TextureType::Texture3D))
    {
        if (getFormatInfo(srcDesc.format).blockSizeInBytes == 12 ||
            getFormatInfo(dstDesc.format).blockSizeInBytes == 12)
        {
            RHI_VALIDATION_ERROR(
                "Copying individual slices of 3D textures with 12B formats is disabled due to poor D3D12 support."
            );
            return;
        }
    }

    baseObject->copyTexture(dst, dstSubresource, dstOffset, src, srcSubresource, srcOffset, extent);
}

Result DebugCommandEncoder::uploadTextureData(
    ITexture* dst,
    SubresourceRange subresourceRange,
    Offset3D offset,
    Extent3D extent,
    const SubresourceData* subresourceData,
    uint32_t subresourceDataCount
)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();

    if (subresourceRange.mipCount != 1)
    {
        if (offset.x != 0 || offset.y != 0 || offset.z != 0)
        {
            RHI_VALIDATION_ERROR("Uploading multiple mip levels at once requires offset to be 0");
            return SLANG_E_INVALID_ARG;
        }

        if (extent.width != kRemainingTextureSize || extent.height != kRemainingTextureSize ||
            extent.depth != kRemainingTextureSize)
        {
            RHI_VALIDATION_ERROR("Uploading multiple mip levels at once requires extent to be Extent3D::kWholeTexture");
            return SLANG_E_INVALID_ARG;
        }
    }

    if (subresourceRange.mipCount * subresourceRange.layerCount != subresourceDataCount)
    {
        RHI_VALIDATION_ERROR("The number of subresource data must match the number of subresources.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->uploadTextureData(dst, subresourceRange, offset, extent, subresourceData, subresourceDataCount);
}

void DebugCommandEncoder::clearBuffer(IBuffer* buffer, BufferRange range)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    if (range.offset % 4 != 0)
    {
        RHI_VALIDATION_ERROR("The range offset must be a multiple of 4.");
        return;
    }
    if (range.size != kEntireBuffer.size && range.size % 4 != 0)
    {
        RHI_VALIDATION_ERROR("The range size must be a multiple of 4.");
        return;
    }
    baseObject->clearBuffer(buffer, range);
}

void DebugCommandEncoder::clearTextureFloat(ITexture* texture, SubresourceRange subresourceRange, float clearValue[4])
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    baseObject->clearTextureFloat(texture, subresourceRange, clearValue);
}

void DebugCommandEncoder::clearTextureUint(ITexture* texture, SubresourceRange subresourceRange, uint32_t clearValue[4])
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    baseObject->clearTextureUint(texture, subresourceRange, clearValue);
}

void DebugCommandEncoder::clearTextureSint(ITexture* texture, SubresourceRange subresourceRange, int32_t clearValue[4])
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    baseObject->clearTextureSint(texture, subresourceRange, clearValue);
}

void DebugCommandEncoder::clearTextureDepthStencil(
    ITexture* texture,
    SubresourceRange subresourceRange,
    bool clearDepth,
    float depthValue,
    bool clearStencil,
    uint8_t stencilValue
)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    const FormatInfo& formatInfo = getFormatInfo(texture->getDesc().format);
    if (!formatInfo.hasDepth && !formatInfo.hasStencil)
    {
        RHI_VALIDATION_ERROR("Texture format does not have depth or stencil");
        return;
    }
    switch (ctx->deviceType)
    {
    case DeviceType::D3D11:
    case DeviceType::D3D12:
        if (!is_set(texture->getDesc().usage, TextureUsage::DepthStencil))
        {
            RHI_VALIDATION_ERROR("Texture needs to have usage flag DepthStencil");
            return;
        }
        break;
    case DeviceType::Vulkan:
        if (!is_set(texture->getDesc().usage, TextureUsage::CopyDestination))
        {
            RHI_VALIDATION_ERROR("Texture needs to have usage flag CopyDestination");
            return;
        }
        break;
    case DeviceType::Metal:
        break;
    case DeviceType::WGPU:
        RHI_VALIDATION_ERROR("Not implemented");
        return;
    case DeviceType::CPU:
    case DeviceType::CUDA:
        RHI_VALIDATION_ERROR("Not supported");
        return;
    default:
        break;
    }
    baseObject->clearTextureDepthStencil(texture, subresourceRange, clearDepth, depthValue, clearStencil, stencilValue);
}

void DebugCommandEncoder::resolveQuery(
    IQueryPool* queryPool,
    uint32_t index,
    uint32_t count,
    IBuffer* buffer,
    uint64_t offset
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
    Size dstRowPitch,
    ITexture* src,
    uint32_t srcLayer,
    uint32_t srcMip,
    Offset3D srcOffset,
    Extent3D extent
)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();

    const TextureDesc& desc = src->getDesc();

    if (srcLayer >= desc.getLayerCount())
    {
        RHI_VALIDATION_ERROR("Src layer is out of bounds.");
        return;
    }
    if (srcMip >= desc.mipCount)
    {
        RHI_VALIDATION_ERROR("Src mip is out of bounds.");
        return;
    }

    baseObject->copyTextureToBuffer(dst, dstOffset, dstSize, dstRowPitch, src, srcLayer, srcMip, srcOffset, extent);
}

void DebugCommandEncoder::copyBufferToTexture(
    ITexture* dst,
    uint32_t dstLayer,
    uint32_t dstMip,
    Offset3D dstOffset,
    IBuffer* src,
    Offset srcOffset,
    Size srcSize,
    Size srcRowPitch,
    Extent3D extent
)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();

    const TextureDesc& desc = dst->getDesc();

    if (dstLayer >= desc.getLayerCount())
    {
        RHI_VALIDATION_ERROR("The base array layer is out of bounds.");
        return;
    }
    if (dstMip >= desc.mipCount)
    {
        RHI_VALIDATION_ERROR("Mip level is out of bounds.");
        return;
    }

    baseObject->copyBufferToTexture(dst, dstLayer, dstMip, dstOffset, src, srcOffset, srcSize, srcRowPitch, extent);
}

void DebugCommandEncoder::buildAccelerationStructure(
    const AccelerationStructureBuildDesc& desc,
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    BufferOffsetPair scratchBuffer,
    uint32_t propertyQueryCount,
    const AccelerationStructureQueryDesc* queryDescs
)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    std::vector<AccelerationStructureQueryDesc> innerQueryDescs;
    for (uint32_t i = 0; i < propertyQueryCount; ++i)
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
    uint32_t accelerationStructureCount,
    IAccelerationStructure** accelerationStructures,
    uint32_t queryCount,
    const AccelerationStructureQueryDesc* queryDescs
)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    std::vector<AccelerationStructureQueryDesc> innerQueryDescs;
    for (uint32_t i = 0; i < queryCount; ++i)
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

void DebugCommandEncoder::serializeAccelerationStructure(BufferOffsetPair dst, IAccelerationStructure* src)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    baseObject->serializeAccelerationStructure(dst, src);
}

void DebugCommandEncoder::deserializeAccelerationStructure(IAccelerationStructure* dst, BufferOffsetPair src)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    baseObject->deserializeAccelerationStructure(dst, src);
}

void DebugCommandEncoder::convertCooperativeVectorMatrix(
    IBuffer* dstBuffer,
    const CooperativeVectorMatrixDesc* dstDescs,
    IBuffer* srcBuffer,
    const CooperativeVectorMatrixDesc* srcDescs,
    uint32_t matrixCount
)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();

    if (!dstBuffer)
    {
        RHI_VALIDATION_ERROR("Destination buffer must be valid");
        return;
    }
    if (!srcBuffer)
    {
        RHI_VALIDATION_ERROR("Source buffer must be valid");
        return;
    }

    SLANG_RETURN_VOID_ON_FAIL(validateConvertCooperativeVectorMatrix(
        ctx,
        dstBuffer->getDesc().size,
        dstDescs,
        srcBuffer->getDesc().size,
        srcDescs,
        matrixCount
    ));

    baseObject->convertCooperativeVectorMatrix(dstBuffer, dstDescs, srcBuffer, srcDescs, matrixCount);
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

void DebugCommandEncoder::globalBarrier()
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    baseObject->globalBarrier();
}

void DebugCommandEncoder::pushDebugGroup(const char* name, const MarkerColor& color)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    baseObject->pushDebugGroup(name, color);
}

void DebugCommandEncoder::popDebugGroup()
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    baseObject->popDebugGroup();
}

void DebugCommandEncoder::insertDebugMarker(const char* name, const MarkerColor& color)
{
    SLANG_RHI_API_FUNC;
    requireOpen();
    requireNoPass();
    baseObject->insertDebugMarker(name, color);
}

void DebugCommandEncoder::writeTimestamp(IQueryPool* pool, uint32_t index)
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
