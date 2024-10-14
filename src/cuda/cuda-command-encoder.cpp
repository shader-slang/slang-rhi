#include "cuda-command-encoder.h"
#include "cuda-command-buffer.h"
#include "cuda-command-queue.h"
#include "cuda-device.h"
#include "cuda-buffer.h"
#include "cuda-acceleration-structure.h"
#include "cuda-query.h"

namespace rhi::cuda {

void CommandEncoderImpl::init(DeviceImpl* device)
{
    m_device = device;
    m_commandBuffer = new CommandBufferImpl();
}

Result CommandEncoderImpl::createRootShaderObject(ShaderProgram* program, ShaderObjectBase** outObject)
{
    RefPtr<ShaderObjectBase> object;
    SLANG_RETURN_ON_FAIL(m_device->createRootShaderObject(program, object.writeRef()));
    // Root objects need to be kept alive until command buffer submission.
    m_commandBuffer->encodeObject(object);
    returnRefPtr(outObject, object);
    return SLANG_OK;
}

void CommandEncoderImpl::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    m_commandBuffer->copyBuffer(dst, dstOffset, src, srcOffset, size);
}

void CommandEncoderImpl::uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data)
{
    m_commandBuffer->uploadBufferData(dst, offset, size, data);
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
    SLANG_UNUSED(buffer);
    SLANG_UNUSED(range);
    SLANG_RHI_UNIMPLEMENTED("clearBuffer");
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
    SLANG_RHI_UNIMPLEMENTED("clearBuffer");
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
    SLANG_UNUSED(desc);
}

void CommandEncoderImpl::endRenderPass() {}

void CommandEncoderImpl::setRenderState(const RenderState& state)
{
    SLANG_UNUSED(state);
}

void CommandEncoderImpl::draw(const DrawArguments& args)
{
    SLANG_UNUSED(args);
}

void CommandEncoderImpl::drawIndexed(const DrawArguments& args)
{
    SLANG_UNUSED(args);
}

void CommandEncoderImpl::drawIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    SLANG_UNUSED(maxDrawCount);
    SLANG_UNUSED(argBuffer);
    SLANG_UNUSED(argOffset);
    SLANG_UNUSED(countBuffer);
    SLANG_UNUSED(countOffset);
}

void CommandEncoderImpl::drawIndexedIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    SLANG_UNUSED(maxDrawCount);
    SLANG_UNUSED(argBuffer);
    SLANG_UNUSED(argOffset);
    SLANG_UNUSED(countBuffer);
    SLANG_UNUSED(countOffset);
}

void CommandEncoderImpl::drawMeshTasks(int x, int y, int z)
{
    SLANG_UNUSED(x);
    SLANG_UNUSED(y);
    SLANG_UNUSED(z);
}

void CommandEncoderImpl::setComputeState(const ComputeState& state)
{
    m_commandBuffer->setComputeState(state);
}

void CommandEncoderImpl::dispatchCompute(int x, int y, int z)
{
    m_commandBuffer->dispatchCompute(x, y, z);
}

void CommandEncoderImpl::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    SLANG_UNUSED(argBuffer);
    SLANG_UNUSED(offset);
    SLANG_RHI_UNIMPLEMENTED("dispatchComputeIndirect");
}

#if SLANG_RHI_ENABLE_OPTIX

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
    AccelerationStructureBuildInputBuilder builder;
    SLANG_RETURN_VOID_ON_FAIL(builder.build(desc, m_device->m_debugCallback));

    AccelerationStructureImpl* dstImpl = checked_cast<AccelerationStructureImpl*>(dst);

    short_vector<OptixAccelEmitDesc, 8> emittedProperties;
    for (GfxCount i = 0; i < propertyQueryCount; i++)
    {
        if (queryDescs[i].queryType == QueryType::AccelerationStructureCompactedSize)
        {
            PlainBufferProxyQueryPoolImpl* queryPool =
                checked_cast<PlainBufferProxyQueryPoolImpl*>(queryDescs[i].queryPool);
            OptixAccelEmitDesc property = {};
            property.type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
            property.result = queryPool->m_buffer + queryDescs[i].firstQueryIndex * sizeof(uint64_t);
            emittedProperties.push_back(property);
        }
    }

    optixAccelBuild(
        m_device->m_ctx.optixContext,
        nullptr, // TODO: CUDA stream
        &builder.buildOptions,
        builder.buildInputs.data(),
        builder.buildInputs.size(),
        scratchBuffer.getDeviceAddress(),
        checked_cast<BufferImpl*>(scratchBuffer.buffer)->m_desc.size - scratchBuffer.offset,
        dstImpl->m_buffer,
        dstImpl->m_desc.size,
        &dstImpl->m_handle,
        emittedProperties.data(),
        emittedProperties.size()
    );
}

void CommandEncoderImpl::copyAccelerationStructure(
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    AccelerationStructureCopyMode mode
)
{
    AccelerationStructureImpl* dstImpl = checked_cast<AccelerationStructureImpl*>(dst);
    AccelerationStructureImpl* srcImpl = checked_cast<AccelerationStructureImpl*>(src);

    switch (mode)
    {
    case AccelerationStructureCopyMode::Clone:
    {
#if 0
        OptixRelocationInfo relocInfo = {};
        optixAccelGetRelocationInfo(m_commandBuffer->m_device->m_ctx.optixContext, srcImpl->m_handle, &relocInfo);

        // TODO setup inputs
        OptixRelocateInput relocInput = {};

        cuMemcpyDtoD(dstImpl->m_buffer, srcImpl->m_buffer, srcImpl->m_desc.size);

        optixAccelRelocate(
            m_commandBuffer->m_device->m_ctx.optixContext,
            nullptr, // TODO: CUDA stream
            &relocInfo,
            &relocInput,
            1,
            dstImpl->m_buffer,
            dstImpl->m_desc.size,
            &dstImpl->m_handle
        );
        break;
#endif
    }
    case AccelerationStructureCopyMode::Compact:
        optixAccelCompact(
            m_device->m_ctx.optixContext,
            nullptr, // TODO: CUDA stream
            srcImpl->m_handle,
            dstImpl->m_buffer,
            dstImpl->m_desc.size,
            &dstImpl->m_handle
        );
        break;
    }
}

void CommandEncoderImpl::queryAccelerationStructureProperties(
    GfxCount accelerationStructureCount,
    IAccelerationStructure* const* accelerationStructures,
    GfxCount queryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
}

void CommandEncoderImpl::serializeAccelerationStructure(BufferWithOffset dst, IAccelerationStructure* src) {}

void CommandEncoderImpl::deserializeAccelerationStructure(IAccelerationStructure* dst, BufferWithOffset src) {}

#else // SLANG_RHI_ENABLE_OPTIX

void CommandEncoderImpl::setRayTracingState(const RayTracingState& state)
{
    SLANG_UNUSED(state);
    SLANG_RHI_UNIMPLEMENTED("setRayTracingState");
}

void CommandEncoderImpl::dispatchRays(GfxIndex raygenShaderIndex, GfxCount width, GfxCount height, GfxCount depth)
{
    SLANG_UNUSED(raygenShaderIndex);
    SLANG_UNUSED(width);
    SLANG_UNUSED(height);
    SLANG_UNUSED(depth);
}

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
}

void CommandEncoderImpl::serializeAccelerationStructure(BufferWithOffset dst, IAccelerationStructure* src)
{
    SLANG_UNUSED(dst);
    SLANG_UNUSED(src);
}

void CommandEncoderImpl::deserializeAccelerationStructure(IAccelerationStructure* dst, BufferWithOffset src)
{
    SLANG_UNUSED(dst);
    SLANG_UNUSED(src);
}

#endif // SLANG_RHI_ENABLE_OPTIX

void CommandEncoderImpl::setBufferState(IBuffer* buffer, ResourceState state)
{
    SLANG_UNUSED(buffer);
    SLANG_UNUSED(state);
}

void CommandEncoderImpl::setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state)
{
    SLANG_UNUSED(texture);
    SLANG_UNUSED(subresourceRange);
    SLANG_UNUSED(state);
}

void CommandEncoderImpl::beginDebugEvent(const char* name, float rgbColor[3])
{
    SLANG_UNUSED(name);
    SLANG_UNUSED(rgbColor);
}

void CommandEncoderImpl::endDebugEvent() {}

void CommandEncoderImpl::writeTimestamp(IQueryPool* pool, GfxIndex index)
{
    m_commandBuffer->writeTimestamp(pool, index);
}

Result CommandEncoderImpl::finish(ICommandBuffer** outCommandBuffer)
{
    if (!m_commandBuffer)
    {
        return SLANG_FAIL;
    }
    returnComPtr(outCommandBuffer, m_commandBuffer);
    m_commandBuffer = nullptr;
    return SLANG_OK;
}

Result CommandEncoderImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

} // namespace rhi::cuda
