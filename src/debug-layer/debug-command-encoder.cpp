#include "debug-command-encoder.h"
#include "debug-command-buffer.h"
#include "debug-helper-functions.h"
#include "debug-query.h"

#include <vector>

namespace rhi::debug {

// ----------------------------------------------------------------------------
// DebugRenderPassEncoder
// ----------------------------------------------------------------------------

DebugRenderPassEncoder::DebugRenderPassEncoder(DebugContext* ctx, DebugCommandEncoder* commandEncoder)
    : UnownedDebugObject<IRenderPassEncoder>(ctx)
    , m_commandEncoder(commandEncoder)
{
    m_rootObject = new DebugRootShaderObject(ctx);
}

IShaderObject* DebugRenderPassEncoder::bindPipeline(IRenderPipeline* pipeline)
{
    SLANG_RHI_DEBUG_API(IRenderPassEncoder, bindPipeline);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();

    if (!pipeline)
    {
        RHI_VALIDATION_ERROR("'pipeline' must not be null.");
        return nullptr;
    }

    m_rootObject->reset();
    m_rootObject->baseObject = baseObject->bindPipeline(pipeline);
    m_pipelineBound = true;

    return m_rootObject;
}

void DebugRenderPassEncoder::bindPipeline(IRenderPipeline* pipeline, IShaderObject* rootObject)
{
    SLANG_RHI_DEBUG_API(IRenderPassEncoder, bindPipeline);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();

    if (!pipeline)
    {
        RHI_VALIDATION_ERROR("'pipeline' must not be null.");
        return;
    }

    baseObject->bindPipeline(pipeline, getInnerObj(rootObject));
    m_pipelineBound = true;
}

void DebugRenderPassEncoder::setRenderState(const RenderState& state)
{
    SLANG_RHI_DEBUG_API(IRenderPassEncoder, setRenderState);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();

    if (state.viewportCount > SLANG_COUNT_OF(state.viewports))
    {
        RHI_VALIDATION_ERROR("Too many viewports (max 16).");
        return;
    }
    if (state.scissorRectCount > SLANG_COUNT_OF(state.scissorRects))
    {
        RHI_VALIDATION_ERROR("Too many scissor rects (max 16).");
        return;
    }
    if (state.vertexBufferCount > SLANG_COUNT_OF(state.vertexBuffers))
    {
        RHI_VALIDATION_ERROR("Too many vertex buffers (max 16).");
        return;
    }

    for (uint32_t i = 0; i < state.viewportCount; ++i)
    {
        if (state.viewports[i].extentX <= 0.0f || state.viewports[i].extentY <= 0.0f)
        {
            RHI_VALIDATION_WARNING("Viewport has non-positive width or height.");
        }
    }

    for (uint32_t i = 0; i < state.vertexBufferCount; ++i)
    {
        if (!state.vertexBuffers[i].buffer)
        {
            RHI_VALIDATION_WARNING("Vertex buffer binding is null.");
        }
    }

    if (!isValidIndexFormat(state.indexFormat))
    {
        RHI_VALIDATION_ERROR("Invalid index format.");
        return;
    }

    m_indexBufferBound = (state.indexBuffer.buffer != nullptr);

    baseObject->setRenderState(state);
}

void DebugRenderPassEncoder::draw(const DrawArguments& args)
{
    SLANG_RHI_DEBUG_API(IRenderPassEncoder, draw);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();

    if (!m_pipelineBound)
    {
        RHI_VALIDATION_ERROR("No pipeline bound.");
        return;
    }
    if (args.instanceCount == 0)
    {
        RHI_VALIDATION_WARNING("instanceCount is 0.");
    }

    baseObject->draw(args);
}

void DebugRenderPassEncoder::drawIndexed(const DrawArguments& args)
{
    SLANG_RHI_DEBUG_API(IRenderPassEncoder, drawIndexed);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();

    if (!m_pipelineBound)
    {
        RHI_VALIDATION_ERROR("No pipeline bound.");
        return;
    }
    if (!m_indexBufferBound)
    {
        RHI_VALIDATION_ERROR("No index buffer bound.");
        return;
    }
    if (args.instanceCount == 0)
    {
        RHI_VALIDATION_WARNING("instanceCount is 0.");
    }

    baseObject->drawIndexed(args);
}

void DebugRenderPassEncoder::drawIndirect(
    uint32_t maxDrawCount,
    BufferOffsetPair argBuffer,
    BufferOffsetPair countBuffer
)
{
    SLANG_RHI_DEBUG_API(IRenderPassEncoder, drawIndirect);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();

    if (!m_pipelineBound)
    {
        RHI_VALIDATION_ERROR("No pipeline bound.");
        return;
    }
    if (!argBuffer.buffer)
    {
        RHI_VALIDATION_ERROR("'argBuffer' must not be null.");
        return;
    }
    if (maxDrawCount == 0)
    {
        RHI_VALIDATION_WARNING("maxDrawCount is 0.");
    }

    baseObject->drawIndirect(maxDrawCount, argBuffer, countBuffer);
}

void DebugRenderPassEncoder::drawIndexedIndirect(
    uint32_t maxDrawCount,
    BufferOffsetPair argBuffer,
    BufferOffsetPair countBuffer
)
{
    SLANG_RHI_DEBUG_API(IRenderPassEncoder, drawIndexedIndirect);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();

    if (!m_pipelineBound)
    {
        RHI_VALIDATION_ERROR("No pipeline bound.");
        return;
    }
    if (!m_indexBufferBound)
    {
        RHI_VALIDATION_ERROR("No index buffer bound.");
        return;
    }
    if (!argBuffer.buffer)
    {
        RHI_VALIDATION_ERROR("'argBuffer' must not be null.");
        return;
    }
    if (maxDrawCount == 0)
    {
        RHI_VALIDATION_WARNING("maxDrawCount is 0.");
    }

    baseObject->drawIndexedIndirect(maxDrawCount, argBuffer, countBuffer);
}

void DebugRenderPassEncoder::drawMeshTasks(uint32_t x, uint32_t y, uint32_t z)
{
    SLANG_RHI_DEBUG_API(IRenderPassEncoder, drawMeshTasks);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();

    if (!m_pipelineBound)
    {
        RHI_VALIDATION_ERROR("No pipeline bound.");
        return;
    }
    if (x == 0 || y == 0 || z == 0)
    {
        RHI_VALIDATION_WARNING("One or more dimensions are 0 (no-op dispatch).");
    }

    baseObject->drawMeshTasks(x, y, z);
}

void DebugRenderPassEncoder::pushDebugGroup(const char* name, const MarkerColor& color)
{
    SLANG_RHI_DEBUG_API(IRenderPassEncoder, pushDebugGroup);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();

    baseObject->pushDebugGroup(name, color);
}

void DebugRenderPassEncoder::popDebugGroup()
{
    SLANG_RHI_DEBUG_API(IRenderPassEncoder, popDebugGroup);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();

    baseObject->popDebugGroup();
}

void DebugRenderPassEncoder::insertDebugMarker(const char* name, const MarkerColor& color)
{
    SLANG_RHI_DEBUG_API(IRenderPassEncoder, insertDebugMarker);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();

    baseObject->insertDebugMarker(name, color);
}

void DebugRenderPassEncoder::writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex)
{
    SLANG_RHI_DEBUG_API(IRenderPassEncoder, writeTimestamp);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();

    if (!queryPool)
    {
        RHI_VALIDATION_ERROR("'queryPool' must not be null.");
        return;
    }
    if (queryIndex >= queryPool->getDesc().count)
    {
        RHI_VALIDATION_ERROR("'queryIndex' is out of range.");
        return;
    }

    baseObject->writeTimestamp(getInnerObj(queryPool), queryIndex);
}

void DebugRenderPassEncoder::end()
{
    SLANG_RHI_DEBUG_API(IRenderPassEncoder, end);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRenderPass();
    m_commandEncoder->m_passState = DebugCommandEncoder::PassState::NoPass;

    baseObject->end();
}

// ----------------------------------------------------------------------------
// DebugComputePassEncoder
// ----------------------------------------------------------------------------

DebugComputePassEncoder::DebugComputePassEncoder(DebugContext* ctx, DebugCommandEncoder* commandEncoder)
    : UnownedDebugObject<IComputePassEncoder>(ctx)
    , m_commandEncoder(commandEncoder)
{
    m_rootObject = new DebugRootShaderObject(ctx);
}

IShaderObject* DebugComputePassEncoder::bindPipeline(IComputePipeline* pipeline)
{
    SLANG_RHI_DEBUG_API(IComputePassEncoder, bindPipeline);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();

    if (!pipeline)
    {
        RHI_VALIDATION_ERROR("'pipeline' must not be null.");
        return nullptr;
    }

    m_rootObject->reset();
    m_rootObject->baseObject = baseObject->bindPipeline(pipeline);
    m_pipelineBound = true;

    return m_rootObject;
}

void DebugComputePassEncoder::bindPipeline(IComputePipeline* pipeline, IShaderObject* rootObject)
{
    SLANG_RHI_DEBUG_API(IComputePassEncoder, bindPipeline);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();

    if (!pipeline)
    {
        RHI_VALIDATION_ERROR("'pipeline' must not be null.");
        return;
    }

    baseObject->bindPipeline(pipeline, getInnerObj(rootObject));
    m_pipelineBound = true;
}

void DebugComputePassEncoder::dispatchCompute(uint32_t x, uint32_t y, uint32_t z)
{
    SLANG_RHI_DEBUG_API(IComputePassEncoder, dispatchCompute);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();

    if (!m_pipelineBound)
    {
        RHI_VALIDATION_ERROR("No pipeline bound.");
        return;
    }
    if (x == 0 || y == 0 || z == 0)
    {
        RHI_VALIDATION_WARNING("One or more group dimensions is 0.");
    }

    baseObject->dispatchCompute(x, y, z);
}

void DebugComputePassEncoder::dispatchComputeIndirect(BufferOffsetPair argBuffer)
{
    SLANG_RHI_DEBUG_API(IComputePassEncoder, dispatchComputeIndirect);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();

    if (!m_pipelineBound)
    {
        RHI_VALIDATION_ERROR("No pipeline bound.");
        return;
    }
    if (!argBuffer.buffer)
    {
        RHI_VALIDATION_ERROR("'argBuffer' must not be null.");
        return;
    }

    baseObject->dispatchComputeIndirect(argBuffer);
}

void DebugComputePassEncoder::pushDebugGroup(const char* name, const MarkerColor& color)
{
    SLANG_RHI_DEBUG_API(IComputePassEncoder, pushDebugGroup);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();

    baseObject->pushDebugGroup(name, color);
}

void DebugComputePassEncoder::popDebugGroup()
{
    SLANG_RHI_DEBUG_API(IComputePassEncoder, popDebugGroup);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();

    baseObject->popDebugGroup();
}

void DebugComputePassEncoder::insertDebugMarker(const char* name, const MarkerColor& color)
{
    SLANG_RHI_DEBUG_API(IComputePassEncoder, insertDebugMarker);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();

    baseObject->insertDebugMarker(name, color);
}

void DebugComputePassEncoder::writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex)
{
    SLANG_RHI_DEBUG_API(IComputePassEncoder, writeTimestamp);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();

    if (!queryPool)
    {
        RHI_VALIDATION_ERROR("'queryPool' must not be null.");
        return;
    }
    if (queryIndex >= queryPool->getDesc().count)
    {
        RHI_VALIDATION_ERROR("'queryIndex' is out of range.");
        return;
    }

    baseObject->writeTimestamp(getInnerObj(queryPool), queryIndex);
}

void DebugComputePassEncoder::end()
{
    SLANG_RHI_DEBUG_API(IComputePassEncoder, end);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireComputePass();
    m_commandEncoder->m_passState = DebugCommandEncoder::PassState::NoPass;

    baseObject->end();
}

// ----------------------------------------------------------------------------
// DebugRayTracingPassEncoder
// ----------------------------------------------------------------------------

DebugRayTracingPassEncoder::DebugRayTracingPassEncoder(DebugContext* ctx, DebugCommandEncoder* commandEncoder)
    : UnownedDebugObject<IRayTracingPassEncoder>(ctx)
    , m_commandEncoder(commandEncoder)
{
    m_rootObject = new DebugRootShaderObject(ctx);
}

IShaderObject* DebugRayTracingPassEncoder::bindPipeline(IRayTracingPipeline* pipeline, IShaderTable* shaderTable)
{
    SLANG_RHI_DEBUG_API(IRayTracingPassEncoder, bindPipeline);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();

    if (!pipeline)
    {
        RHI_VALIDATION_ERROR("'pipeline' must not be null.");
        return nullptr;
    }
    if (!shaderTable)
    {
        RHI_VALIDATION_ERROR("'shaderTable' must not be null.");
        return nullptr;
    }

    m_rootObject->reset();
    m_rootObject->baseObject = baseObject->bindPipeline(pipeline, shaderTable);
    m_pipelineBound = true;

    return m_rootObject;
}

void DebugRayTracingPassEncoder::bindPipeline(
    IRayTracingPipeline* pipeline,
    IShaderTable* shaderTable,
    IShaderObject* rootObject
)
{
    SLANG_RHI_DEBUG_API(IRayTracingPassEncoder, bindPipeline);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();

    if (!pipeline)
    {
        RHI_VALIDATION_ERROR("'pipeline' must not be null.");
        return;
    }
    if (!shaderTable)
    {
        RHI_VALIDATION_ERROR("'shaderTable' must not be null.");
        return;
    }

    baseObject->bindPipeline(pipeline, shaderTable, getInnerObj(rootObject));
    m_pipelineBound = true;
}

void DebugRayTracingPassEncoder::dispatchRays(
    uint32_t rayGenShaderIndex,
    uint32_t width,
    uint32_t height,
    uint32_t depth
)
{
    SLANG_RHI_DEBUG_API(IRayTracingPassEncoder, dispatchRays);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();

    if (!m_pipelineBound)
    {
        RHI_VALIDATION_ERROR("No pipeline bound.");
        return;
    }
    if (width == 0 || height == 0 || depth == 0)
    {
        RHI_VALIDATION_WARNING("One or more dispatch dimensions is 0.");
    }

    baseObject->dispatchRays(rayGenShaderIndex, width, height, depth);
}

void DebugRayTracingPassEncoder::pushDebugGroup(const char* name, const MarkerColor& color)
{
    SLANG_RHI_DEBUG_API(IRayTracingPassEncoder, pushDebugGroup);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();

    baseObject->pushDebugGroup(name, color);
}

void DebugRayTracingPassEncoder::popDebugGroup()
{
    SLANG_RHI_DEBUG_API(IRayTracingPassEncoder, popDebugGroup);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();

    baseObject->popDebugGroup();
}

void DebugRayTracingPassEncoder::insertDebugMarker(const char* name, const MarkerColor& color)
{
    SLANG_RHI_DEBUG_API(IRayTracingPassEncoder, insertDebugMarker);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();

    baseObject->insertDebugMarker(name, color);
}

void DebugRayTracingPassEncoder::writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex)
{
    SLANG_RHI_DEBUG_API(IRayTracingPassEncoder, writeTimestamp);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();

    if (!queryPool)
    {
        RHI_VALIDATION_ERROR("'queryPool' must not be null.");
        return;
    }
    if (queryIndex >= queryPool->getDesc().count)
    {
        RHI_VALIDATION_ERROR("'queryIndex' is out of range.");
        return;
    }

    baseObject->writeTimestamp(getInnerObj(queryPool), queryIndex);
}

void DebugRayTracingPassEncoder::end()
{
    SLANG_RHI_DEBUG_API(IRayTracingPassEncoder, end);

    m_commandEncoder->requireOpen();
    m_commandEncoder->requireRayTracingPass();
    m_commandEncoder->m_passState = DebugCommandEncoder::PassState::NoPass;

    baseObject->end();
}

// ----------------------------------------------------------------------------
// DebugCommandEncoder
// ----------------------------------------------------------------------------

DebugCommandEncoder::DebugCommandEncoder(DebugContext* ctx)
    : DebugObject<ICommandEncoder>(ctx)
    , m_renderPassEncoder(ctx, this)
    , m_computePassEncoder(ctx, this)
    , m_rayTracingPassEncoder(ctx, this)
{
}

const CommandEncoderDesc& DebugCommandEncoder::getDesc()
{
    return baseObject->getDesc();
}

IRenderPassEncoder* DebugCommandEncoder::beginRenderPass(const RenderPassDesc& desc)
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, beginRenderPass);

    requireOpen();
    requireNoPass();

    bool hasErrors = false;

    // Validate color attachment count.
    if (desc.colorAttachmentCount > 8)
    {
        RHI_VALIDATION_ERROR("Too many color attachments (max 8).");
        return nullptr;
    }

    // Track sample count consistency across all attachments.
    uint32_t commonSampleCount = 0;

    auto checkSampleCount = [&](uint32_t sc, const char* attachmentName)
    {
        if (commonSampleCount == 0)
            commonSampleCount = sc;
        else if (sc != commonSampleCount)
        {
            RHI_VALIDATION_ERROR_FORMAT(
                "Sample count mismatch across attachments (%s has %u, expected %u).",
                attachmentName,
                sc,
                commonSampleCount
            );
            hasErrors = true;
        }
    };

    // Validate color attachments.
    for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i)
    {
        const auto& attachment = desc.colorAttachments[i];
        if (!attachment.view)
            continue;

        if (!isValidLoadOp(attachment.loadOp))
        {
            RHI_VALIDATION_ERROR_FORMAT("Color attachment %u has invalid loadOp.", i);
            hasErrors = true;
        }
        if (!isValidStoreOp(attachment.storeOp))
        {
            RHI_VALIDATION_ERROR_FORMAT("Color attachment %u has invalid storeOp.", i);
            hasErrors = true;
        }

        ITexture* texture = attachment.view->getTexture();
        if (texture)
        {
            const TextureDesc& texDesc = texture->getDesc();

            // Check usage.
            if (!is_set(texDesc.usage, TextureUsage::RenderTarget))
            {
                RHI_VALIDATION_ERROR_FORMAT("Color attachment %u texture does not have RenderTarget usage.", i);
                hasErrors = true;
            }

            // Determine effective format (view format overrides texture format if not Undefined).
            Format effectiveFormat = attachment.view->getDesc().format;
            if (effectiveFormat == Format::Undefined)
                effectiveFormat = texDesc.format;

            // Check that it's not a depth/stencil format.
            const FormatInfo& formatInfo = getFormatInfo(effectiveFormat);
            if (formatInfo.hasDepth || formatInfo.hasStencil)
            {
                RHI_VALIDATION_ERROR_FORMAT(
                    "Color attachment %u uses a depth/stencil format; use depthStencilAttachment instead.",
                    i
                );
                hasErrors = true;
            }

            // Check sample count consistency.
            char name[32];
            snprintf(name, sizeof(name), "color attachment %u", i);
            checkSampleCount(texDesc.sampleCount, name);
        }
    }

    // Validate depth/stencil attachment.
    if (desc.depthStencilAttachment && desc.depthStencilAttachment->view)
    {
        const auto& dsAttachment = *desc.depthStencilAttachment;

        if (!isValidLoadOp(dsAttachment.depthLoadOp))
        {
            RHI_VALIDATION_ERROR("Depth/stencil attachment has invalid depthLoadOp.");
            hasErrors = true;
        }
        if (!isValidStoreOp(dsAttachment.depthStoreOp))
        {
            RHI_VALIDATION_ERROR("Depth/stencil attachment has invalid depthStoreOp.");
            hasErrors = true;
        }
        if (!isValidLoadOp(dsAttachment.stencilLoadOp))
        {
            RHI_VALIDATION_ERROR("Depth/stencil attachment has invalid stencilLoadOp.");
            hasErrors = true;
        }
        if (!isValidStoreOp(dsAttachment.stencilStoreOp))
        {
            RHI_VALIDATION_ERROR("Depth/stencil attachment has invalid stencilStoreOp.");
            hasErrors = true;
        }

        ITexture* texture = dsAttachment.view->getTexture();
        if (texture)
        {
            const TextureDesc& texDesc = texture->getDesc();

            // Check usage.
            if (!is_set(texDesc.usage, TextureUsage::DepthStencil))
            {
                RHI_VALIDATION_ERROR("Depth/stencil attachment texture does not have DepthStencil usage.");
                hasErrors = true;
            }

            // Determine effective format.
            Format effectiveFormat = dsAttachment.view->getDesc().format;
            if (effectiveFormat == Format::Undefined)
                effectiveFormat = texDesc.format;

            // Check that it's a depth/stencil format.
            const FormatInfo& formatInfo = getFormatInfo(effectiveFormat);
            if (!formatInfo.hasDepth && !formatInfo.hasStencil)
            {
                RHI_VALIDATION_ERROR("Depth/stencil attachment format is not a depth/stencil format.");
                hasErrors = true;
            }

            // Check sample count consistency.
            checkSampleCount(texDesc.sampleCount, "depth/stencil attachment");
        }
    }

    if (hasErrors)
        return nullptr;

    auto innerEncoder = baseObject->beginRenderPass(desc);
    if (!innerEncoder)
        return nullptr;

    m_passState = PassState::RenderPass;
    m_renderPassEncoder.m_pipelineBound = false;
    m_renderPassEncoder.m_indexBufferBound = false;
    m_renderPassEncoder.baseObject = innerEncoder;

    return &m_renderPassEncoder;
}

IComputePassEncoder* DebugCommandEncoder::beginComputePass()
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, beginComputePass);

    requireOpen();
    requireNoPass();

    auto innerEncoder = baseObject->beginComputePass();
    if (!innerEncoder)
        return nullptr;

    m_passState = PassState::ComputePass;
    m_computePassEncoder.m_pipelineBound = false;
    m_computePassEncoder.baseObject = innerEncoder;

    return &m_computePassEncoder;
}

IRayTracingPassEncoder* DebugCommandEncoder::beginRayTracingPass()
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, beginRayTracingPass);

    requireOpen();
    requireNoPass();

    auto innerEncoder = baseObject->beginRayTracingPass();
    if (!innerEncoder)
        return nullptr;

    m_passState = PassState::RayTracingPass;
    m_rayTracingPassEncoder.m_pipelineBound = false;
    m_rayTracingPassEncoder.baseObject = innerEncoder;

    return &m_rayTracingPassEncoder;
}

void DebugCommandEncoder::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, copyBuffer);

    requireOpen();
    requireNoPass();

    if (!dst)
    {
        RHI_VALIDATION_ERROR("'dst' must not be null.");
        return;
    }
    if (!src)
    {
        RHI_VALIDATION_ERROR("'src' must not be null.");
        return;
    }
    if (size == 0)
    {
        RHI_VALIDATION_WARNING("size is 0.");
        return;
    }
    if (!isValidSubrange(srcOffset, size, src->getDesc().size))
    {
        RHI_VALIDATION_ERROR("Source range out of bounds.");
        return;
    }
    if (!isValidSubrange(dstOffset, size, dst->getDesc().size))
    {
        RHI_VALIDATION_ERROR("Destination range out of bounds.");
        return;
    }
    if (!is_set(src->getDesc().usage, BufferUsage::CopySource))
    {
        RHI_VALIDATION_ERROR("Source buffer does not have CopySource usage flag.");
        return;
    }
    if (!is_set(dst->getDesc().usage, BufferUsage::CopyDestination))
    {
        RHI_VALIDATION_ERROR("Destination buffer does not have CopyDestination usage flag.");
        return;
    }
    if (dst == src)
    {
        // Check for overlapping ranges — overlapping same-buffer copies are undefined behavior
        // on all major APIs (D3D12, Vulkan, Metal), so skip the call to avoid driver errors.
        if (srcOffset < dstOffset + size && dstOffset < srcOffset + size)
        {
            RHI_VALIDATION_WARNING("Overlapping source and destination ranges on same buffer.");
            return;
        }
    }

    baseObject->copyBuffer(dst, dstOffset, src, srcOffset, size);
}

Result DebugCommandEncoder::uploadBufferData(IBuffer* dst, Offset offset, Size size, const void* data)
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, uploadBufferData);

    requireOpen();
    requireNoPass();

    if (!dst)
    {
        RHI_VALIDATION_ERROR("'dst' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (!data)
    {
        RHI_VALIDATION_ERROR("'data' must not be null.");
        return SLANG_E_INVALID_ARG;
    }
    if (size == 0)
    {
        RHI_VALIDATION_WARNING("size is 0.");
        return SLANG_OK;
    }
    if (!isValidSubrange(offset, size, dst->getDesc().size))
    {
        RHI_VALIDATION_ERROR("Destination range out of bounds.");
        return SLANG_E_INVALID_ARG;
    }
    if (!is_set(dst->getDesc().usage, BufferUsage::CopyDestination))
    {
        RHI_VALIDATION_ERROR("Destination buffer does not have CopyDestination usage flag.");
        return SLANG_E_INVALID_ARG;
    }

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
    SLANG_RHI_DEBUG_API(ICommandEncoder, copyTexture);

    requireOpen();
    requireNoPass();

    if (!src)
    {
        RHI_VALIDATION_ERROR("'src' must not be null.");
        return;
    }
    if (!dst)
    {
        RHI_VALIDATION_ERROR("'dst' must not be null.");
        return;
    }

    const TextureDesc& srcDesc = src->getDesc();
    if (srcSubresource.layer >= srcDesc.getLayerCount())
    {
        RHI_VALIDATION_ERROR("Source layer is out of bounds.");
        return;
    }
    if (srcSubresource.mip >= srcDesc.mipCount)
    {
        RHI_VALIDATION_ERROR("Source mip is out of bounds.");
        return;
    }

    const TextureDesc& dstDesc = dst->getDesc();
    if (dstSubresource.layer >= dstDesc.getLayerCount())
    {
        RHI_VALIDATION_ERROR("Destination layer is out of bounds.");
        return;
    }
    if (dstSubresource.mip >= dstDesc.mipCount)
    {
        RHI_VALIDATION_ERROR("Destination mip is out of bounds.");
        return;
    }

    if (srcSubresource.layerCount != dstSubresource.layerCount)
    {
        RHI_VALIDATION_ERROR("Source and destination layer count must match.");
        return;
    }

    if (srcSubresource.mipCount != dstSubresource.mipCount)
    {
        RHI_VALIDATION_ERROR("Source and destination mip count must match.");
        return;
    }

    if (srcSubresource.layerCount == 0 && srcDesc.getLayerCount() != dstDesc.getLayerCount())
    {
        RHI_VALIDATION_ERROR("Copy layer count is 0, so source and destination texture layer count must match.");
        return;
    }

    if (srcSubresource.mipCount == 0 && srcDesc.mipCount != dstDesc.mipCount)
    {
        RHI_VALIDATION_ERROR("Copy mip count is 0, so source and destination texture mip count must match.");
        return;
    }

    if (srcSubresource.mipCount != 1)
    {
        if (!srcOffset.isZero() || !dstOffset.isZero())
        {
            RHI_VALIDATION_ERROR("Copying multiple mip levels at once requires offset to be 0.");
            return;
        }

        if (!extent.isWholeTexture())
        {
            RHI_VALIDATION_ERROR("Copying multiple mip levels at once requires extent to be Extent3D::kWholeTexture.");
            return;
        }
    }

    if (extent.width == kRemainingTextureSize)
    {
        if (srcOffset.x != dstOffset.x)
        {
            RHI_VALIDATION_ERROR(
                "Copying the remaining texture requires source and destination offset to be the same."
            );
            return;
        }
    }
    if (extent.height == kRemainingTextureSize)
    {
        if (srcOffset.y != dstOffset.y)
        {
            RHI_VALIDATION_ERROR(
                "Copying the remaining texture requires source and destination offset to be the same."
            );
            return;
        }
    }
    if (extent.depth == kRemainingTextureSize)
    {
        if (srcOffset.z != dstOffset.z)
        {
            RHI_VALIDATION_ERROR(
                "Copying the remaining texture requires source and destination offset to be the same."
            );
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
    SLANG_RHI_DEBUG_API(ICommandEncoder, uploadTextureData);

    requireOpen();
    requireNoPass();

    if (subresourceRange.mipCount != 1)
    {
        if (offset.x != 0 || offset.y != 0 || offset.z != 0)
        {
            RHI_VALIDATION_ERROR("Uploading multiple mip levels at once requires offset to be 0.");
            return SLANG_E_INVALID_ARG;
        }

        if (extent.width != kRemainingTextureSize || extent.height != kRemainingTextureSize ||
            extent.depth != kRemainingTextureSize)
        {
            RHI_VALIDATION_ERROR(
                "Uploading multiple mip levels at once requires extent to be Extent3D::kWholeTexture."
            );
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
    SLANG_RHI_DEBUG_API(ICommandEncoder, clearBuffer);

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
    SLANG_RHI_DEBUG_API(ICommandEncoder, clearTextureFloat);

    requireOpen();
    requireNoPass();

    if (!texture)
    {
        RHI_VALIDATION_ERROR("'texture' must not be null.");
        return;
    }
    const TextureDesc& desc = texture->getDesc();
    if (!validateSubresourceRange(subresourceRange, desc))
    {
        RHI_VALIDATION_ERROR("Subresource range out of bounds.");
        return;
    }
    const FormatInfo& formatInfo = getFormatInfo(desc.format);
    if (formatInfo.hasDepth || formatInfo.hasStencil)
    {
        RHI_VALIDATION_ERROR(
            "clearTextureFloat cannot be used with depth/stencil formats; use clearTextureDepthStencil."
        );
        return;
    }
    if (!is_set(desc.usage, TextureUsage::RenderTarget) && !is_set(desc.usage, TextureUsage::UnorderedAccess))
    {
        RHI_VALIDATION_ERROR("Texture must have RenderTarget or UnorderedAccess usage.");
        return;
    }
    if (formatInfo.kind != FormatKind::Float && formatInfo.kind != FormatKind::Normalized)
    {
        RHI_VALIDATION_WARNING("clearTextureFloat called on a non-float/non-normalized format.");
    }

    baseObject->clearTextureFloat(texture, subresourceRange, clearValue);
}

void DebugCommandEncoder::clearTextureUint(ITexture* texture, SubresourceRange subresourceRange, uint32_t clearValue[4])
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, clearTextureUint);

    requireOpen();
    requireNoPass();

    if (!texture)
    {
        RHI_VALIDATION_ERROR("'texture' must not be null.");
        return;
    }
    const TextureDesc& desc = texture->getDesc();
    if (!validateSubresourceRange(subresourceRange, desc))
    {
        RHI_VALIDATION_ERROR("Subresource range out of bounds.");
        return;
    }
    const FormatInfo& formatInfo = getFormatInfo(desc.format);
    if (formatInfo.hasDepth || formatInfo.hasStencil)
    {
        RHI_VALIDATION_ERROR(
            "clearTextureUint cannot be used with depth/stencil formats; use clearTextureDepthStencil."
        );
        return;
    }
    if (!is_set(desc.usage, TextureUsage::RenderTarget) && !is_set(desc.usage, TextureUsage::UnorderedAccess))
    {
        RHI_VALIDATION_ERROR("Texture must have RenderTarget or UnorderedAccess usage.");
        return;
    }
    if (formatInfo.kind != FormatKind::Integer || formatInfo.isSigned)
    {
        RHI_VALIDATION_WARNING("clearTextureUint called on a non-unsigned-integer format.");
    }

    baseObject->clearTextureUint(texture, subresourceRange, clearValue);
}

void DebugCommandEncoder::clearTextureSint(ITexture* texture, SubresourceRange subresourceRange, int32_t clearValue[4])
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, clearTextureSint);

    requireOpen();
    requireNoPass();

    if (!texture)
    {
        RHI_VALIDATION_ERROR("'texture' must not be null.");
        return;
    }
    const TextureDesc& desc = texture->getDesc();
    if (!validateSubresourceRange(subresourceRange, desc))
    {
        RHI_VALIDATION_ERROR("Subresource range out of bounds.");
        return;
    }
    const FormatInfo& formatInfo = getFormatInfo(desc.format);
    if (formatInfo.hasDepth || formatInfo.hasStencil)
    {
        RHI_VALIDATION_ERROR(
            "clearTextureSint cannot be used with depth/stencil formats; use clearTextureDepthStencil."
        );
        return;
    }
    if (!is_set(desc.usage, TextureUsage::RenderTarget) && !is_set(desc.usage, TextureUsage::UnorderedAccess))
    {
        RHI_VALIDATION_ERROR("Texture must have RenderTarget or UnorderedAccess usage.");
        return;
    }
    if (formatInfo.kind != FormatKind::Integer || !formatInfo.isSigned)
    {
        RHI_VALIDATION_WARNING("clearTextureSint called on a non-signed-integer format.");
    }

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
    SLANG_RHI_DEBUG_API(ICommandEncoder, clearTextureDepthStencil);

    requireOpen();
    requireNoPass();
    if (!texture)
    {
        RHI_VALIDATION_ERROR("'texture' must not be null.");
        return;
    }
    const TextureDesc& desc = texture->getDesc();
    if (!validateSubresourceRange(subresourceRange, desc))
    {
        RHI_VALIDATION_ERROR("Subresource range out of bounds.");
        return;
    }
    const FormatInfo& formatInfo = getFormatInfo(desc.format);
    if (!formatInfo.hasDepth && !formatInfo.hasStencil)
    {
        RHI_VALIDATION_ERROR("Texture format does not have depth or stencil.");
        return;
    }
    if (!clearDepth && !clearStencil)
    {
        RHI_VALIDATION_WARNING("Both clearDepth and clearStencil are false; nothing to clear.");
        return;
    }
    if (clearDepth && !formatInfo.hasDepth)
    {
        RHI_VALIDATION_WARNING("clearDepth is true but texture format has no depth component.");
    }
    if (clearStencil && !formatInfo.hasStencil)
    {
        RHI_VALIDATION_WARNING("clearStencil is true but texture format has no stencil component.");
    }
    if (clearDepth && (depthValue < 0.0f || depthValue > 1.0f))
    {
        RHI_VALIDATION_WARNING("depthValue is outside [0, 1] range.");
    }
    switch (ctx->deviceType)
    {
    case DeviceType::D3D11:
    case DeviceType::D3D12:
        if (!is_set(desc.usage, TextureUsage::DepthStencil))
        {
            RHI_VALIDATION_ERROR("Texture needs to have usage flag DepthStencil.");
            return;
        }
        break;
    case DeviceType::Vulkan:
        if (!is_set(desc.usage, TextureUsage::CopyDestination))
        {
            RHI_VALIDATION_ERROR("Texture needs to have usage flag CopyDestination.");
            return;
        }
        break;
    case DeviceType::Metal:
        break;
    case DeviceType::WGPU:
        RHI_VALIDATION_ERROR("Not implemented.");
        return;
    case DeviceType::CPU:
    case DeviceType::CUDA:
        RHI_VALIDATION_ERROR("Not supported.");
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
    SLANG_RHI_DEBUG_API(ICommandEncoder, resolveQuery);

    requireOpen();
    requireNoPass();

    if (!queryPool)
    {
        RHI_VALIDATION_ERROR("'queryPool' must not be null.");
        return;
    }
    if (!buffer)
    {
        RHI_VALIDATION_ERROR("'buffer' must not be null.");
        return;
    }
    if (count == 0)
    {
        RHI_VALIDATION_WARNING("count is 0.");
        return;
    }
    if (!isValidSubrange(index, count, queryPool->getDesc().count))
    {
        RHI_VALIDATION_ERROR("Query range out of bounds (index + count exceeds query pool size).");
        return;
    }
    if (!isValidSubrange(offset, count * sizeof(uint64_t), buffer->getDesc().size))
    {
        RHI_VALIDATION_ERROR(
            "Destination range out of bounds (offset + count * sizeof(uint64_t) exceeds buffer size)."
        );
        return;
    }

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
    SLANG_RHI_DEBUG_API(ICommandEncoder, copyTextureToBuffer);

    requireOpen();
    requireNoPass();

    if (!src)
    {
        RHI_VALIDATION_ERROR("'src' must not be null.");
        return;
    }
    if (!dst)
    {
        RHI_VALIDATION_ERROR("'dst' must not be null.");
        return;
    }

    const TextureDesc& desc = src->getDesc();

    if (srcLayer >= desc.getLayerCount())
    {
        RHI_VALIDATION_ERROR("Source layer is out of bounds.");
        return;
    }
    if (srcMip >= desc.mipCount)
    {
        RHI_VALIDATION_ERROR("Source mip is out of bounds.");
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
    SLANG_RHI_DEBUG_API(ICommandEncoder, copyBufferToTexture);

    requireOpen();
    requireNoPass();

    if (!dst)
    {
        RHI_VALIDATION_ERROR("'dst' must not be null.");
        return;
    }
    if (!src)
    {
        RHI_VALIDATION_ERROR("'src' must not be null.");
        return;
    }

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
    SLANG_RHI_DEBUG_API(ICommandEncoder, buildAccelerationStructure);

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
    SLANG_RHI_DEBUG_API(ICommandEncoder, copyAccelerationStructure);

    requireOpen();
    requireNoPass();

    if (!dst)
    {
        RHI_VALIDATION_ERROR("'dst' must not be null.");
        return;
    }
    if (!src)
    {
        RHI_VALIDATION_ERROR("'src' must not be null.");
        return;
    }
    if (!isValidAccelerationStructureCopyMode(mode))
    {
        RHI_VALIDATION_ERROR("Invalid acceleration structure copy mode.");
        return;
    }

    baseObject->copyAccelerationStructure(dst, src, mode);
}

void DebugCommandEncoder::queryAccelerationStructureProperties(
    uint32_t accelerationStructureCount,
    IAccelerationStructure** accelerationStructures,
    uint32_t queryCount,
    const AccelerationStructureQueryDesc* queryDescs
)
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, queryAccelerationStructureProperties);

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
    SLANG_RHI_DEBUG_API(ICommandEncoder, serializeAccelerationStructure);

    requireOpen();
    requireNoPass();

    if (!src)
    {
        RHI_VALIDATION_ERROR("'src' must not be null.");
        return;
    }
    if (!dst.buffer)
    {
        RHI_VALIDATION_ERROR("'dst.buffer' must not be null.");
        return;
    }

    baseObject->serializeAccelerationStructure(dst, src);
}

void DebugCommandEncoder::deserializeAccelerationStructure(IAccelerationStructure* dst, BufferOffsetPair src)
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, deserializeAccelerationStructure);

    requireOpen();
    requireNoPass();

    if (!dst)
    {
        RHI_VALIDATION_ERROR("'dst' must not be null.");
        return;
    }
    if (!src.buffer)
    {
        RHI_VALIDATION_ERROR("'src.buffer' must not be null.");
        return;
    }

    baseObject->deserializeAccelerationStructure(dst, src);
}

void DebugCommandEncoder::executeClusterOperation(const ClusterOperationDesc& desc)
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, executeClusterOperation);

    requireOpen();
    requireNoPass();

    if (SLANG_FAILED(validateClusterOperationParams(ctx, desc.params)))
    {
        return;
    }
    if (!desc.argCountBuffer && desc.params.maxArgCount == 0)
    {
        RHI_VALIDATION_ERROR("'argCountBuffer' is not provided and 'params.maxArgCount' is 0.");
        return;
    }
    if (!desc.argsBuffer)
    {
        RHI_VALIDATION_ERROR("'argsBuffer' must not be null.");
        return;
    }
    if (desc.argsBufferStride == 0)
    {
        RHI_VALIDATION_ERROR("'argsBufferStride' must not be 0.");
        return;
    }
    if (!desc.scratchBuffer)
    {
        RHI_VALIDATION_ERROR("'scratchBuffer' must not be null.");
        return;
    }
    switch (desc.params.mode)
    {
    case ClusterOperationMode::ImplicitDestinations:
        if (!desc.addressesBuffer)
        {
            RHI_VALIDATION_ERROR(
                "'addressesBuffer' must not be null in ClusterOperationMode::ImplicitDestinations mode."
            );
            return;
        }
        if (!desc.resultBuffer)
        {
            RHI_VALIDATION_ERROR("'resultBuffer' must not be null in ClusterOperationMode::ImplicitDestinations mode.");
            return;
        }
        break;
    case ClusterOperationMode::ExplicitDestinations:
        if (!desc.addressesBuffer)
        {
            RHI_VALIDATION_ERROR(
                "'addressesBuffer' must not be null in ClusterOperationMode::ExplicitDestinations mode."
            );
            return;
        }
        break;
    case ClusterOperationMode::GetSizes:
        if (!desc.sizesBuffer)
        {
            RHI_VALIDATION_ERROR("'sizesBuffer' must not be null in ClusterOperationMode::GetSizes mode.");
            return;
        }
        break;
    default:
        RHI_VALIDATION_ERROR("Unknown cluster operation mode.");
        return;
    }

    if (desc.addressesBufferStride != 0 && desc.addressesBufferStride < 8)
    {
        RHI_VALIDATION_ERROR("'addressesBufferStride' must be 0 or >= 8.");
        return;
    }
    if (desc.sizesBufferStride != 0 && desc.sizesBufferStride < 4)
    {
        RHI_VALIDATION_ERROR("'sizesBufferStride' must be 0 or >= 4.");
        return;
    }

    baseObject->executeClusterOperation(desc);
}

void DebugCommandEncoder::convertCooperativeVectorMatrix(
    IBuffer* dstBuffer,
    const CooperativeVectorMatrixDesc* dstDescs,
    IBuffer* srcBuffer,
    const CooperativeVectorMatrixDesc* srcDescs,
    uint32_t matrixCount
)
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, convertCooperativeVectorMatrix);

    requireOpen();
    requireNoPass();

    if (!dstBuffer)
    {
        RHI_VALIDATION_ERROR("'dstBuffer' must not be null.");
        return;
    }
    if (!srcBuffer)
    {
        RHI_VALIDATION_ERROR("'srcBuffer' must not be null.");
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
    SLANG_RHI_DEBUG_API(ICommandEncoder, setBufferState);

    requireOpen();
    requireNoPass();

    if (!buffer)
    {
        RHI_VALIDATION_ERROR("'buffer' must not be null.");
        return;
    }
    if (!isValidResourceState(state))
    {
        RHI_VALIDATION_ERROR("Invalid resource state.");
        return;
    }
    if (state == ResourceState::Undefined)
    {
        RHI_VALIDATION_WARNING("Setting buffer state to Undefined.");
        return;
    }

    baseObject->setBufferState(buffer, state);
}

void DebugCommandEncoder::setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state)
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, setTextureState);

    requireOpen();
    requireNoPass();

    if (!texture)
    {
        RHI_VALIDATION_ERROR("'texture' must not be null.");
        return;
    }
    if (!isValidResourceState(state))
    {
        RHI_VALIDATION_ERROR("Invalid resource state.");
        return;
    }
    if (!validateSubresourceRange(subresourceRange, texture->getDesc()))
    {
        RHI_VALIDATION_ERROR("Subresource range out of bounds.");
        return;
    }
    if (state == ResourceState::Undefined)
    {
        RHI_VALIDATION_WARNING("Setting texture state to Undefined.");
        return;
    }

    baseObject->setTextureState(texture, subresourceRange, state);
}

void DebugCommandEncoder::globalBarrier()
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, globalBarrier);

    requireOpen();
    requireNoPass();

    baseObject->globalBarrier();
}

void DebugCommandEncoder::pushDebugGroup(const char* name, const MarkerColor& color)
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, pushDebugGroup);

    requireOpen();
    requireNoPass();

    baseObject->pushDebugGroup(name, color);
}

void DebugCommandEncoder::popDebugGroup()
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, popDebugGroup);

    requireOpen();
    requireNoPass();

    baseObject->popDebugGroup();
}

void DebugCommandEncoder::insertDebugMarker(const char* name, const MarkerColor& color)
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, insertDebugMarker);

    requireOpen();
    requireNoPass();

    baseObject->insertDebugMarker(name, color);
}

void DebugCommandEncoder::writeTimestamp(IQueryPool* pool, uint32_t index)
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, writeTimestamp);

    requireOpen();

    if (!pool)
    {
        RHI_VALIDATION_ERROR("'queryPool' must not be null.");
        return;
    }
    if (index >= pool->getDesc().count)
    {
        RHI_VALIDATION_ERROR("'queryIndex' is out of range.");
        return;
    }

    baseObject->writeTimestamp(getInnerObj(pool), index);
}

Result DebugCommandEncoder::finish(const CommandBufferDesc& desc, ICommandBuffer** outCommandBuffer)
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, finish);

    requireOpen();
    requireNoPass();
    RefPtr<DebugCommandBuffer> outObject = new DebugCommandBuffer(ctx);
    SLANG_RETURN_ON_FAIL(baseObject->finish(desc, outObject->baseObject.writeRef()));

    returnComPtr(outCommandBuffer, outObject);
    return SLANG_OK;
}

Result DebugCommandEncoder::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_DEBUG_API(ICommandEncoder, getNativeHandle);

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
