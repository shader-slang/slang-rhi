#include "cuda-command-encoder.h"
#include "cuda-command-buffer.h"
#include "cuda-device.h"
#include "cuda-buffer.h"
#include "cuda-acceleration-structure.h"
#include "cuda-query.h"

namespace rhi::cuda {

// PassEncoderImpl

void PassEncoderImpl::init(CommandBufferImpl* cmdBuffer)
{
    m_writer = cmdBuffer;
}

void PassEncoderImpl::setBufferState(IBuffer* buffer, ResourceState state)
{
    SLANG_UNUSED(buffer);
    SLANG_UNUSED(state);
}

void PassEncoderImpl::setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state)
{
    SLANG_UNUSED(texture);
    SLANG_UNUSED(subresourceRange);
    SLANG_UNUSED(state);
}

void PassEncoderImpl::beginDebugEvent(const char* name, float rgbColor[3])
{
    SLANG_UNUSED(name);
    SLANG_UNUSED(rgbColor);
}

void PassEncoderImpl::endDebugEvent() {}

void PassEncoderImpl::writeTimestamp(IQueryPool* pool, GfxIndex index)
{
    m_writer->writeTimestamp(pool, index);
}

// ResourcePassEncoderImpl

void ResourcePassEncoderImpl::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    m_writer->copyBuffer(dst, dstOffset, src, srcOffset, size);
}

void ResourcePassEncoderImpl::uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data)
{
    m_writer->uploadBufferData(dst, offset, size, data);
}

void ResourcePassEncoderImpl::copyTexture(
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

void ResourcePassEncoderImpl::uploadTextureData(
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

void ResourcePassEncoderImpl::clearBuffer(IBuffer* buffer, const BufferRange* range)
{
    SLANG_UNUSED(buffer);
    SLANG_UNUSED(range);
    SLANG_RHI_UNIMPLEMENTED("clearBuffer");
}

void ResourcePassEncoderImpl::clearTexture(
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

void ResourcePassEncoderImpl::resolveQuery(
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

void ResourcePassEncoderImpl::copyTextureToBuffer(
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

// ComputePassEncoderImpl

void ComputePassEncoderImpl::init(CommandBufferImpl* cmdBuffer)
{
    m_writer = cmdBuffer;
    m_commandBuffer = cmdBuffer;
}

Result ComputePassEncoderImpl::bindPipeline(IPipeline* state, IShaderObject** outRootObject)
{
    m_writer->setPipeline(state);
    Pipeline* pipelineImpl = checked_cast<Pipeline*>(state);
    SLANG_RETURN_ON_FAIL(
        m_commandBuffer->m_device->createRootShaderObject(pipelineImpl->m_program, m_rootObject.writeRef())
    );
    returnComPtr(outRootObject, m_rootObject);
    return SLANG_OK;
}

Result ComputePassEncoderImpl::bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject)
{
    m_writer->setPipeline(state);
    Pipeline* pipelineImpl = checked_cast<Pipeline*>(state);
    SLANG_RETURN_ON_FAIL(
        m_commandBuffer->m_device->createRootShaderObject(pipelineImpl->m_program, m_rootObject.writeRef())
    );
    m_rootObject->copyFrom(rootObject, m_commandBuffer->m_transientHeap);
    return SLANG_OK;
}

Result ComputePassEncoderImpl::dispatchCompute(int x, int y, int z)
{
    m_writer->bindRootShaderObject(m_rootObject);
    m_writer->dispatchCompute(x, y, z);
    return SLANG_OK;
}

Result ComputePassEncoderImpl::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    SLANG_RHI_UNIMPLEMENTED("dispatchComputeIndirect");
}

// RayTracingPassEncoderImpl

#if SLANG_RHI_HAS_OPTIX

void RayTracingPassEncoderImpl::init(CommandBufferImpl* cmdBuffer)
{
    m_writer = cmdBuffer;
    m_commandBuffer = cmdBuffer;
}

void RayTracingPassEncoderImpl::buildAccelerationStructure(
    const AccelerationStructureBuildDesc& desc,
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    BufferWithOffset scratchBuffer,
    GfxCount propertyQueryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    AccelerationStructureBuildInputBuilder builder;
    SLANG_RETURN_VOID_ON_FAIL(builder.build(desc, m_commandBuffer->m_device->m_debugCallback));

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
        m_commandBuffer->m_device->m_ctx.optixContext,
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

void RayTracingPassEncoderImpl::copyAccelerationStructure(
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
            m_commandBuffer->m_device->m_ctx.optixContext,
            nullptr, // TODO: CUDA stream
            srcImpl->m_handle,
            dstImpl->m_buffer,
            dstImpl->m_desc.size,
            &dstImpl->m_handle
        );
        break;
    }
}

void RayTracingPassEncoderImpl::queryAccelerationStructureProperties(
    GfxCount accelerationStructureCount,
    IAccelerationStructure* const* accelerationStructures,
    GfxCount queryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
}

void RayTracingPassEncoderImpl::serializeAccelerationStructure(BufferWithOffset dst, IAccelerationStructure* src) {}

void RayTracingPassEncoderImpl::deserializeAccelerationStructure(IAccelerationStructure* dst, BufferWithOffset src) {}

Result RayTracingPassEncoderImpl::bindPipeline(IPipeline* pipeline, IShaderObject** outRootObject)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

Result RayTracingPassEncoderImpl::bindPipelineWithRootObject(IPipeline* pipeline, IShaderObject* rootObject)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

Result RayTracingPassEncoderImpl::dispatchRays(
    GfxIndex raygenShaderIndex,
    IShaderTable* shaderTable,
    GfxCount width,
    GfxCount height,
    GfxCount depth
)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

#endif // SLANG_RHI_HAS_OPTIX

} // namespace rhi::cuda
