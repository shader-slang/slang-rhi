#include "command-buffer.h"

#include "rhi-shared.h"
#include "device.h"
#include "format-conversion.h"

namespace rhi {

// ----------------------------------------------------------------------------
// RenderPassEncoder
// ----------------------------------------------------------------------------

IRenderPassEncoder* RenderPassEncoder::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IRenderPassEncoder::getTypeGuid())
        return static_cast<IRenderPassEncoder*>(this);
    return nullptr;
}

RenderPassEncoder::RenderPassEncoder(CommandEncoder* commandEncoder)
    : m_commandEncoder(commandEncoder)
{
}

void RenderPassEncoder::writeRenderState()
{
    commands::SetRenderState cmd;
    cmd.state = m_renderState;
    cmd.pipeline = m_pipeline;
    m_commandEncoder->getPipelineSpecializationArgs(m_pipeline, m_rootObject, cmd.specializationArgs);
    if (SLANG_FAILED(m_commandEncoder->getBindingData(m_rootObject, cmd.bindingData)))
    {
        m_commandEncoder->getDevice()
            ->handleMessage(DebugMessageType::Error, DebugMessageSource::Layer, "Failed to get binding data");
        return;
    }
    m_commandList->write(std::move(cmd));
}

IShaderObject* RenderPassEncoder::bindPipeline(IRenderPipeline* pipeline)
{
    if (m_commandList)
    {
        m_pipeline = pipeline;
        ShaderProgram* program = checked_cast<ShaderProgram*>(pipeline->getProgram());
        if (SLANG_FAILED(m_commandEncoder->getDevice()->createRootShaderObject(program, m_rootObject.writeRef())))
            return nullptr;
        return m_rootObject;
    }
    return nullptr;
}

void RenderPassEncoder::bindPipeline(IRenderPipeline* pipeline, IShaderObject* rootObject)
{
    if (m_commandList)
    {
        m_pipeline = checked_cast<RenderPipeline*>(pipeline);
        m_rootObject = checked_cast<RootShaderObject*>(rootObject);
    }
}

void RenderPassEncoder::setRenderState(const RenderState& state)
{
    if (m_commandList)
    {
        m_renderState = state;
        // Retain resources now instead of when writing the SetRenderState command.
        // This is needed to allow clients to release resources after calling setRenderState
        // but before issuing any draw calls.
        for (uint32_t i = 0; i < m_renderState.vertexBufferCount; ++i)
            m_commandList->retainResource<Buffer>(m_renderState.vertexBuffers[i].buffer);
        m_commandList->retainResource<Buffer>(m_renderState.indexBuffer.buffer);
    }
}

void RenderPassEncoder::draw(const DrawArguments& args)
{
    if (m_commandList)
    {
        writeRenderState();
        commands::Draw cmd;
        cmd.args = args;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::drawIndexed(const DrawArguments& args)
{
    if (m_commandList)
    {
        writeRenderState();
        commands::DrawIndexed cmd;
        cmd.args = args;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::drawIndirect(uint32_t maxDrawCount, BufferOffsetPair argBuffer, BufferOffsetPair countBuffer)
{
    if (m_commandList)
    {
        writeRenderState();
        commands::DrawIndirect cmd;
        cmd.maxDrawCount = maxDrawCount;
        cmd.argBuffer = argBuffer;
        cmd.countBuffer = countBuffer;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::drawIndexedIndirect(
    uint32_t maxDrawCount,
    BufferOffsetPair argBuffer,
    BufferOffsetPair countBuffer
)
{
    if (m_commandList)
    {
        writeRenderState();
        commands::DrawIndexedIndirect cmd;
        cmd.maxDrawCount = maxDrawCount;
        cmd.argBuffer = argBuffer;
        cmd.countBuffer = countBuffer;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::drawMeshTasks(uint32_t x, uint32_t y, uint32_t z)
{
    if (m_commandList)
    {
        writeRenderState();
        commands::DrawMeshTasks cmd;
        cmd.x = x;
        cmd.y = y;
        cmd.z = z;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::pushDebugGroup(const char* name, const MarkerColor& color)
{
    if (m_commandList)
    {
        commands::PushDebugGroup cmd;
        cmd.name = name;
        cmd.color = color;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::popDebugGroup()
{
    if (m_commandList)
    {
        commands::PopDebugGroup cmd;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::insertDebugMarker(const char* name, const MarkerColor& color)
{
    if (m_commandList)
    {
        commands::InsertDebugMarker cmd;
        cmd.name = name;
        cmd.color = color;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex)
{
    if (m_commandList)
    {
        commands::WriteTimestamp cmd;
        cmd.queryPool = checked_cast<QueryPool*>(queryPool);
        cmd.queryIndex = queryIndex;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::end()
{
    if (m_commandList)
    {
        commands::EndRenderPass cmd;
        m_commandList->write(std::move(cmd));
        m_commandList = nullptr;
    }
}

// ----------------------------------------------------------------------------
// ComputePassEncoder
// ----------------------------------------------------------------------------

IComputePassEncoder* ComputePassEncoder::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IComputePassEncoder::getTypeGuid())
        return static_cast<IComputePassEncoder*>(this);
    return nullptr;
}

ComputePassEncoder::ComputePassEncoder(CommandEncoder* commandEncoder)
    : m_commandEncoder(commandEncoder)
{
}

void ComputePassEncoder::writeComputeState()
{
    commands::SetComputeState cmd;
    cmd.pipeline = m_pipeline;
    m_commandEncoder->getPipelineSpecializationArgs(m_pipeline, m_rootObject, cmd.specializationArgs);
    if (SLANG_FAILED(m_commandEncoder->getBindingData(m_rootObject, cmd.bindingData)))
    {
        m_commandEncoder->getDevice()
            ->handleMessage(DebugMessageType::Error, DebugMessageSource::Layer, "Failed to get binding data");
        return;
    }
    m_commandList->write(std::move(cmd));
}

IShaderObject* ComputePassEncoder::bindPipeline(IComputePipeline* pipeline)
{
    if (m_commandList)
    {
        m_pipeline = pipeline;
        ShaderProgram* program = checked_cast<ShaderProgram*>(pipeline->getProgram());
        if (SLANG_FAILED(m_commandEncoder->getDevice()->createRootShaderObject(program, m_rootObject.writeRef())))
            return nullptr;
        return m_rootObject;
    }
    return nullptr;
}

void ComputePassEncoder::bindPipeline(IComputePipeline* pipeline, IShaderObject* rootObject)
{
    if (m_commandList)
    {
        m_pipeline = checked_cast<ComputePipeline*>(pipeline);
        m_rootObject = checked_cast<RootShaderObject*>(rootObject);
    }
}

void ComputePassEncoder::dispatchCompute(uint32_t x, uint32_t y, uint32_t z)
{
    if (m_commandList)
    {
        writeComputeState();
        commands::DispatchCompute cmd;
        cmd.x = x;
        cmd.y = y;
        cmd.z = z;
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::dispatchComputeIndirect(BufferOffsetPair argBuffer)
{
    if (m_commandList)
    {
        writeComputeState();
        commands::DispatchComputeIndirect cmd;
        cmd.argBuffer = argBuffer;
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::pushDebugGroup(const char* name, const MarkerColor& color)
{
    if (m_commandList)
    {
        commands::PushDebugGroup cmd;
        cmd.name = name;
        cmd.color = color;
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::popDebugGroup()
{
    if (m_commandList)
    {
        commands::PopDebugGroup cmd;
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::insertDebugMarker(const char* name, const MarkerColor& color)
{
    if (m_commandList)
    {
        commands::InsertDebugMarker cmd;
        cmd.name = name;
        cmd.color = color;
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex)
{
    if (m_commandList)
    {
        commands::WriteTimestamp cmd;
        cmd.queryPool = checked_cast<QueryPool*>(queryPool);
        cmd.queryIndex = queryIndex;
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::end()
{
    if (m_commandList)
    {
        commands::EndComputePass cmd;
        m_commandList->write(std::move(cmd));
        m_commandList = nullptr;
    }
}

// ----------------------------------------------------------------------------
// RayTracingPassEncoder
// ----------------------------------------------------------------------------

IRayTracingPassEncoder* RayTracingPassEncoder::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IRayTracingPassEncoder::getTypeGuid())
        return static_cast<IRayTracingPassEncoder*>(this);
    return nullptr;
}

RayTracingPassEncoder::RayTracingPassEncoder(CommandEncoder* commandEncoder)
    : m_commandEncoder(commandEncoder)
{
}

void RayTracingPassEncoder::writeRayTracingState()
{
    commands::SetRayTracingState cmd;
    cmd.pipeline = m_pipeline;
    cmd.shaderTable = m_shaderTable;
    m_commandEncoder->getPipelineSpecializationArgs(m_pipeline, m_rootObject, cmd.specializationArgs);
    if (SLANG_FAILED(m_commandEncoder->getBindingData(m_rootObject, cmd.bindingData)))
    {
        m_commandEncoder->getDevice()
            ->handleMessage(DebugMessageType::Error, DebugMessageSource::Layer, "Failed to get binding data");
        return;
    }

    m_commandList->write(std::move(cmd));
}

IShaderObject* RayTracingPassEncoder::bindPipeline(IRayTracingPipeline* pipeline, IShaderTable* shaderTable)
{
    if (m_commandList)
    {
        m_pipeline = pipeline;
        m_shaderTable = shaderTable;
        ShaderProgram* program = checked_cast<ShaderProgram*>(pipeline->getProgram());
        if (SLANG_FAILED(m_commandEncoder->getDevice()->createRootShaderObject(program, m_rootObject.writeRef())))
            return nullptr;
        return m_rootObject;
    }
    return nullptr;
}

void RayTracingPassEncoder::bindPipeline(
    IRayTracingPipeline* pipeline,
    IShaderTable* shaderTable,
    IShaderObject* rootObject
)
{
    if (m_commandList)
    {
        m_pipeline = checked_cast<RayTracingPipeline*>(pipeline);
        m_shaderTable = checked_cast<ShaderTable*>(shaderTable);
        m_rootObject = checked_cast<RootShaderObject*>(rootObject);
    }
}

void RayTracingPassEncoder::dispatchRays(uint32_t rayGenShaderIndex, uint32_t width, uint32_t height, uint32_t depth)
{
    if (m_commandList)
    {
        writeRayTracingState();
        commands::DispatchRays cmd;
        cmd.rayGenShaderIndex = rayGenShaderIndex;
        cmd.width = width;
        cmd.height = height;
        cmd.depth = depth;
        m_commandList->write(std::move(cmd));
    }
}

void RayTracingPassEncoder::pushDebugGroup(const char* name, const MarkerColor& color)
{
    if (m_commandList)
    {
        commands::PushDebugGroup cmd;
        cmd.name = name;
        cmd.color = color;
        m_commandList->write(std::move(cmd));
    }
}

void RayTracingPassEncoder::popDebugGroup()
{
    if (m_commandList)
    {
        commands::PopDebugGroup cmd;
        m_commandList->write(std::move(cmd));
    }
}

void RayTracingPassEncoder::insertDebugMarker(const char* name, const MarkerColor& color)
{
    if (m_commandList)
    {
        commands::InsertDebugMarker cmd;
        cmd.name = name;
        cmd.color = color;
        m_commandList->write(std::move(cmd));
    }
}

void RayTracingPassEncoder::writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex)
{
    if (m_commandList)
    {
        commands::WriteTimestamp cmd;
        cmd.queryPool = checked_cast<QueryPool*>(queryPool);
        cmd.queryIndex = queryIndex;
        m_commandList->write(std::move(cmd));
    }
}

void RayTracingPassEncoder::end()
{
    if (m_commandList)
    {
        commands::EndRayTracingPass cmd;
        m_commandList->write(std::move(cmd));
        m_commandList = nullptr;
    }
}

// ----------------------------------------------------------------------------
// CommandEncoder
// ----------------------------------------------------------------------------

ICommandEncoder* CommandEncoder::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == ICommandEncoder::getTypeGuid())
        return static_cast<ICommandEncoder*>(this);
    return nullptr;
}

IRenderPassEncoder* CommandEncoder::beginRenderPass(const RenderPassDesc& desc)
{
    commands::BeginRenderPass cmd;
    cmd.desc = desc;
    m_commandList->write(std::move(cmd));
    m_renderPassEncoder.m_commandList = m_commandList;
    return &m_renderPassEncoder;
}

IComputePassEncoder* CommandEncoder::beginComputePass()
{
    commands::BeginComputePass cmd;
    m_commandList->write(std::move(cmd));
    m_computePassEncoder.m_commandList = m_commandList;
    return &m_computePassEncoder;
}

IRayTracingPassEncoder* CommandEncoder::beginRayTracingPass()
{
    commands::BeginRayTracingPass cmd;
    m_commandList->write(std::move(cmd));
    m_rayTracingPassEncoder.m_commandList = m_commandList;
    return &m_rayTracingPassEncoder;
}

void CommandEncoder::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    commands::CopyBuffer cmd;
    cmd.dst = dst;
    cmd.dstOffset = dstOffset;
    cmd.src = src;
    cmd.srcOffset = srcOffset;
    cmd.size = size;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::copyTexture(
    ITexture* dst,
    SubresourceRange dstSubresource,
    Offset3D dstOffset,
    ITexture* src,
    SubresourceRange srcSubresource,
    Offset3D srcOffset,
    Extent3D extent
)
{
    commands::CopyTexture cmd;
    cmd.dst = dst;
    cmd.dstSubresource = dstSubresource;
    cmd.dstOffset = dstOffset;
    cmd.src = src;
    cmd.srcSubresource = srcSubresource;
    cmd.srcOffset = srcOffset;
    cmd.extent = extent;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::copyTextureToBuffer(
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
    commands::CopyTextureToBuffer cmd;
    cmd.dst = dst;
    cmd.dstOffset = dstOffset;
    cmd.dstSize = dstSize;
    cmd.dstRowPitch = dstRowPitch;
    cmd.src = src;
    cmd.srcLayer = srcLayer;
    cmd.srcMip = srcMip;
    cmd.srcOffset = srcOffset;
    cmd.extent = extent;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::copyBufferToTexture(
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
    SubresourceLayout* layout = static_cast<SubresourceLayout*>(m_commandList->allocData(sizeof(SubresourceLayout)));

    // Get basic layout info from the texture
    Texture* textureImpl = checked_cast<Texture*>(dst);
    textureImpl->getSubresourceRegionLayout(dstMip, dstOffset, extent, kDefaultAlignment, layout);

    // The layout that actually matters is the layout of the buffer, defined
    // by the row stride, so recalculate it given srcRowPitch.
    layout->rowPitch = srcRowPitch;
    layout->slicePitch = layout->rowCount * layout->rowPitch;
    layout->sizeInBytes = layout->slicePitch * layout->size.depth;
    SLANG_RHI_ASSERT(srcSize >= layout->sizeInBytes)

    // Add texture upload command for just this layer/mip
    commands::UploadTextureData cmd;
    cmd.dst = dst;
    cmd.subresourceRange = {dstLayer, 1, dstMip, 1};
    cmd.offset = dstOffset;
    cmd.extent = extent;
    cmd.layouts = layout;
    cmd.srcBuffer = src;
    cmd.srcOffset = srcOffset;
    m_commandList->write(std::move(cmd));
}

Result CommandEncoder::uploadTextureData(
    ITexture* dst,
    SubresourceRange subresourceRange,
    Offset3D offset,
    Extent3D extent,
    const SubresourceData* subresourceData,
    uint32_t subresourceDataCount
)
{
    // Allocate space in command list memory for layout information on each subresource.
    SubresourceLayout* layouts =
        static_cast<SubresourceLayout*>(m_commandList->allocData(sizeof(SubresourceLayout) * subresourceDataCount));

    // Get texture
    Texture* textureImpl = checked_cast<Texture*>(dst);

    // Gather subresource layout for each layer/mip and sum up total required staging buffer size.
    Size totalSize = 0;
    {
        SubresourceLayout* srLayout = layouts;
        for (uint32_t layerOffset = 0; layerOffset < subresourceRange.layerCount; layerOffset++)
        {
            for (uint32_t mipOffset = 0; mipOffset < subresourceRange.mipCount; mipOffset++)
            {
                uint32_t mip = subresourceRange.mip + mipOffset;

                textureImpl->getSubresourceRegionLayout(mip, offset, extent, kDefaultAlignment, srLayout);
                totalSize += srLayout->sizeInBytes;
                srLayout++;
            }
        }
    }

    // Allocate and retain a staging buffer for the upload.
    RefPtr<StagingHeap::Handle> handle;
    SLANG_RETURN_ON_FAIL(getDevice()->m_uploadHeap.allocHandle(totalSize, {}, handle.writeRef()));
    m_commandList->retainResource(handle);

    // Copy subresources a row at a time into the staging buffer.
    uint8_t* dstData;
    void* mappedData;
    SLANG_RETURN_ON_FAIL(handle->map(&mappedData));
    dstData = (uint8_t*)mappedData;
    {
        // Iterate over sub resources by layer and mip level
        SubresourceLayout* srLayout = layouts;
        const SubresourceData* srSrcData = subresourceData;
        uint8_t* srDestData = dstData;
        for (uint32_t layerOffset = 0; layerOffset < subresourceRange.layerCount; layerOffset++)
        {
            for (uint32_t mipOffset = 0; mipOffset < subresourceRange.mipCount; mipOffset++)
            {
                // Source and dest rows may have different alignments, so its valid for strides to be
                // different (even if data itself isn't). We copy the minimum of the two to avoid
                // reading/writing out of bounds.
                Size rowCopyPitch = min(srSrcData->rowPitch, srLayout->rowPitch);

                // Iterate over slices and rows, copying a row at a time
                uint8_t* sliceSrcData = (uint8_t*)srSrcData->data;
                uint8_t* sliceDestData = srDestData;
                for (uint32_t slice = 0; slice < srLayout->size.depth; slice++)
                {
                    uint8_t* rowSrcData = sliceSrcData;
                    uint8_t* rowDestData = sliceDestData;
                    for (uint32_t row = 0; row < srLayout->rowCount; row++)
                    {
                        // Copy the row.
                        memcpy(rowDestData, rowSrcData, rowCopyPitch);

                        // Increment src/dest position within this slice.
                        rowSrcData += srSrcData->rowPitch;
                        rowDestData += srLayout->rowPitch;
                    }

                    // Increment src/dest position of current slice.
                    sliceSrcData += srSrcData->slicePitch;
                    sliceDestData += srLayout->slicePitch;
                }

                // Move to next subresource.
                srDestData += srLayout->sizeInBytes;
                srSrcData++;
                srLayout++;
            }
        }
    }
    handle->unmap();

    // Store command that contains the basic parameters plus info
    // on layouts and the staging buffer.
    commands::UploadTextureData cmd;
    cmd.dst = dst;
    cmd.subresourceRange = subresourceRange;
    cmd.offset = offset;
    cmd.extent = extent;
    cmd.layouts = layouts;
    cmd.srcBuffer = handle->getBuffer();
    cmd.srcOffset = handle->getOffset();
    m_commandList->write(std::move(cmd));
    return SLANG_OK;
}

Result CommandEncoder::uploadBufferData(IBuffer* dst, Offset offset, Size size, const void* data)
{
    RefPtr<StagingHeap::Handle> handle;
    SLANG_RETURN_ON_FAIL(getDevice()->m_uploadHeap.stageHandle(data, size, {}, handle.writeRef()));

    m_commandList->retainResource(handle);

    commands::CopyBuffer cmd;

    cmd.dst = dst;
    cmd.dstOffset = offset;
    cmd.src = handle->getBuffer();
    cmd.srcOffset = handle->getOffset();
    cmd.size = size;

    m_commandList->write(std::move(cmd));
    return SLANG_OK;
}

void CommandEncoder::clearBuffer(IBuffer* buffer, BufferRange range)
{
    commands::ClearBuffer cmd;
    cmd.buffer = buffer;
    cmd.range = checked_cast<Buffer*>(buffer)->resolveBufferRange(range);
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::clearTextureFloat(ITexture* texture, SubresourceRange subresourceRange, float clearValue[4])
{
    commands::ClearTextureFloat cmd;
    cmd.texture = texture;
    cmd.subresourceRange = checked_cast<Texture*>(texture)->resolveSubresourceRange(subresourceRange);
    ::memcpy(cmd.clearValue, clearValue, sizeof(cmd.clearValue));
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::clearTextureUint(ITexture* texture, SubresourceRange subresourceRange, uint32_t clearValue[4])
{
    commands::ClearTextureUint cmd;
    cmd.texture = texture;
    cmd.subresourceRange = checked_cast<Texture*>(texture)->resolveSubresourceRange(subresourceRange);
    ::memcpy(cmd.clearValue, clearValue, sizeof(cmd.clearValue));
    // Different APIs/drivers appear to handle out-of-range clear values differently.
    // To ensure consistent results we clamp the values here.
    FormatConversionFuncs funcs = getFormatConversionFuncs(texture->getDesc().format);
    if (funcs.clampIntFunc)
    {
        funcs.clampIntFunc(cmd.clearValue);
    }
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::clearTextureSint(ITexture* texture, SubresourceRange subresourceRange, int32_t clearValue[4])
{
    commands::ClearTextureUint cmd;
    cmd.texture = texture;
    cmd.subresourceRange = checked_cast<Texture*>(texture)->resolveSubresourceRange(subresourceRange);
    ::memcpy(cmd.clearValue, clearValue, sizeof(cmd.clearValue));
    // Different APIs/drivers appear to handle out-of-range clear values differently.
    // To ensure consistent results we clamp the values here.
    FormatConversionFuncs funcs = getFormatConversionFuncs(texture->getDesc().format);
    if (funcs.clampIntFunc)
    {
        funcs.clampIntFunc(cmd.clearValue);
    }
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::clearTextureDepthStencil(
    ITexture* texture,
    SubresourceRange subresourceRange,
    bool clearDepth,
    float depthValue,
    bool clearStencil,
    uint8_t stencilValue
)
{
    if (!clearDepth && !clearStencil)
        return;
    commands::ClearTextureDepthStencil cmd;
    cmd.texture = texture;
    cmd.subresourceRange = checked_cast<Texture*>(texture)->resolveSubresourceRange(subresourceRange);
    cmd.clearDepth = clearDepth;
    cmd.depthValue = depthValue;
    cmd.clearStencil = clearStencil;
    cmd.stencilValue = stencilValue;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::resolveQuery(
    IQueryPool* queryPool,
    uint32_t index,
    uint32_t count,
    IBuffer* buffer,
    uint64_t offset
)
{
    commands::ResolveQuery cmd;
    cmd.queryPool = queryPool;
    cmd.index = index;
    cmd.count = count;
    cmd.buffer = buffer;
    cmd.offset = offset;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::buildAccelerationStructure(
    const AccelerationStructureBuildDesc& desc,
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    BufferOffsetPair scratchBuffer,
    uint32_t propertyQueryCount,
    const AccelerationStructureQueryDesc* queryDescs
)
{
    commands::BuildAccelerationStructure cmd;
    cmd.desc = desc;
    cmd.dst = dst;
    cmd.src = src;
    cmd.scratchBuffer = scratchBuffer;
    cmd.propertyQueryCount = propertyQueryCount;
    cmd.queryDescs = queryDescs;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::copyAccelerationStructure(
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    AccelerationStructureCopyMode mode
)
{
    commands::CopyAccelerationStructure cmd;
    cmd.dst = checked_cast<AccelerationStructure*>(dst);
    cmd.src = checked_cast<AccelerationStructure*>(src);
    cmd.mode = mode;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::queryAccelerationStructureProperties(
    uint32_t accelerationStructureCount,
    IAccelerationStructure** accelerationStructures,
    uint32_t queryCount,
    const AccelerationStructureQueryDesc* queryDescs
)
{
    SLANG_RHI_UNIMPLEMENTED("queryAccelerationStructureProperties");
}

void CommandEncoder::serializeAccelerationStructure(BufferOffsetPair dst, IAccelerationStructure* src)
{
    commands::SerializeAccelerationStructure cmd;
    cmd.dst = dst;
    cmd.src = checked_cast<AccelerationStructure*>(src);
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::deserializeAccelerationStructure(IAccelerationStructure* dst, BufferOffsetPair src)
{
    commands::DeserializeAccelerationStructure cmd;
    cmd.dst = checked_cast<AccelerationStructure*>(dst);
    cmd.src = src;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::executeClusterOperation(const ClusterOperationDesc& desc)
{
    commands::ExecuteClusterOperation cmd;
    cmd.desc = desc;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::convertCooperativeVectorMatrix(
    IBuffer* dstBuffer,
    const CooperativeVectorMatrixDesc* dstDescs,
    IBuffer* srcBuffer,
    const CooperativeVectorMatrixDesc* srcDescs,
    uint32_t matrixCount
)
{
    commands::ConvertCooperativeVectorMatrix cmd;
    cmd.dstBuffer = checked_cast<Buffer*>(dstBuffer);
    cmd.dstDescs = dstDescs;
    cmd.srcBuffer = checked_cast<Buffer*>(srcBuffer);
    cmd.srcDescs = srcDescs;
    cmd.matrixCount = matrixCount;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::setBufferState(IBuffer* buffer, ResourceState state)
{
    commands::SetBufferState cmd;
    cmd.buffer = checked_cast<Buffer*>(buffer);
    cmd.state = state;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state)
{
    commands::SetTextureState cmd;
    cmd.texture = checked_cast<Texture*>(texture);
    cmd.subresourceRange = subresourceRange;
    cmd.state = state;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::globalBarrier()
{
    commands::GlobalBarrier cmd;
    m_commandList->write(std::move(cmd));
}


void CommandEncoder::pushDebugGroup(const char* name, const MarkerColor& color)
{
    commands::PushDebugGroup cmd;
    cmd.name = name;
    cmd.color = color;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::popDebugGroup()
{
    commands::PopDebugGroup cmd;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::insertDebugMarker(const char* name, const MarkerColor& color)
{
    commands::InsertDebugMarker cmd;
    cmd.name = name;
    cmd.color = color;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex)
{
    commands::WriteTimestamp cmd;
    cmd.queryPool = checked_cast<QueryPool*>(queryPool);
    cmd.queryIndex = queryIndex;
    m_commandList->write(std::move(cmd));
}

Result CommandEncoder::finish(ICommandBuffer** outCommandBuffer)
{
    // iterate over commands and specialize pipelines
    return SLANG_FAIL;
}

Result CommandEncoder::getPipelineSpecializationArgs(
    IPipeline* pipeline,
    IShaderObject* object,
    ExtendedShaderObjectTypeListObject*& outSpecializationArgs
)
{
    if (checked_cast<ShaderProgram*>(pipeline->getProgram())->isSpecializable())
    {
        RootShaderObject* rootObject = checked_cast<RootShaderObject*>(object);
        RefPtr<ExtendedShaderObjectTypeListObject> specializationArgs = new ExtendedShaderObjectTypeListObject();
        rootObject->collectSpecializationArgs(*specializationArgs);
        m_pipelineSpecializationArgs.push_back(specializationArgs);
        outSpecializationArgs = specializationArgs.get();
    }
    else
    {
        outSpecializationArgs = nullptr;
    }
    return SLANG_OK;
}

Result CommandEncoder::resolvePipelines(Device* device)
{
    CommandList* commandList = m_commandList;
    auto command = commandList->getCommands();
    while (command)
    {
        if (command->id == CommandID::SetRenderState)
        {
            auto& cmd = commandList->getCommand<commands::SetRenderState>(command);
            RenderPipeline* pipeline = checked_cast<RenderPipeline*>(cmd.pipeline);
            auto specializationArgs = static_cast<ExtendedShaderObjectTypeListObject*>(cmd.specializationArgs);
            Pipeline* concretePipeline = nullptr;
            SLANG_RETURN_ON_FAIL(device->getConcretePipeline(pipeline, specializationArgs, concretePipeline));
            cmd.pipeline = static_cast<RenderPipeline*>(concretePipeline);
            cmd.specializationArgs = nullptr;
        }
        else if (command->id == CommandID::SetComputeState)
        {
            auto& cmd = commandList->getCommand<commands::SetComputeState>(command);
            ComputePipeline* pipeline = checked_cast<ComputePipeline*>(cmd.pipeline);
            auto specializationArgs = static_cast<ExtendedShaderObjectTypeListObject*>(cmd.specializationArgs);
            Pipeline* concretePipeline = nullptr;
            SLANG_RETURN_ON_FAIL(device->getConcretePipeline(pipeline, specializationArgs, concretePipeline));
            cmd.pipeline = static_cast<ComputePipeline*>(concretePipeline);
            cmd.specializationArgs = nullptr;
        }
        else if (command->id == CommandID::SetRayTracingState)
        {
            auto& cmd = commandList->getCommand<commands::SetRayTracingState>(command);
            RayTracingPipeline* pipeline = checked_cast<RayTracingPipeline*>(cmd.pipeline);
            auto specializationArgs = static_cast<ExtendedShaderObjectTypeListObject*>(cmd.specializationArgs);
            Pipeline* concretePipeline = nullptr;
            SLANG_RETURN_ON_FAIL(device->getConcretePipeline(pipeline, specializationArgs, concretePipeline));
            cmd.pipeline = static_cast<RayTracingPipeline*>(concretePipeline);
            cmd.specializationArgs = nullptr;
        }
        command = command->next;
    }
    return SLANG_OK;
}

// ----------------------------------------------------------------------------
// CommandBuffer
// ----------------------------------------------------------------------------

ICommandBuffer* CommandBuffer::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == ICommandBuffer::getTypeGuid())
        return static_cast<ICommandBuffer*>(this);
    return nullptr;
}

} // namespace rhi
