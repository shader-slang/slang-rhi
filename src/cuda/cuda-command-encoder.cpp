#include "cuda-command-encoder.h"
#include "cuda-command-buffer.h"
#include "cuda-device.h"

namespace rhi::cuda {

// CommandEncoderImpl

void CommandEncoderImpl::init(CommandBufferImpl* cmdBuffer)
{
    m_writer = cmdBuffer;
}

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
    m_writer->writeTimestamp(pool, index);
}

// ResourceCommandEncoderImpl

void ResourceCommandEncoderImpl::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    m_writer->copyBuffer(dst, dstOffset, src, srcOffset, size);
}

void ResourceCommandEncoderImpl::uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data)
{
    m_writer->uploadBufferData(dst, offset, size, data);
}

void ResourceCommandEncoderImpl::copyTexture(
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

void ResourceCommandEncoderImpl::uploadTextureData(
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

void ResourceCommandEncoderImpl::clearBuffer(IBuffer* buffer, const BufferRange* range)
{
    SLANG_UNUSED(buffer);
    SLANG_UNUSED(range);
    SLANG_RHI_UNIMPLEMENTED("clearBuffer");
}

void ResourceCommandEncoderImpl::clearTexture(
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

void ResourceCommandEncoderImpl::resolveQuery(
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

void ResourceCommandEncoderImpl::copyTextureToBuffer(
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

// ComputeCommandEncoderImpl

void ComputeCommandEncoderImpl::init(CommandBufferImpl* cmdBuffer)
{
    m_writer = cmdBuffer;
    m_commandBuffer = cmdBuffer;
}

Result ComputeCommandEncoderImpl::bindPipeline(IPipeline* state, IShaderObject** outRootObject)
{
    m_writer->setPipeline(state);
    Pipeline* pipelineImpl = static_cast<Pipeline*>(state);
    SLANG_RETURN_ON_FAIL(
        m_commandBuffer->m_device->createRootShaderObject(pipelineImpl->m_program, m_rootObject.writeRef())
    );
    returnComPtr(outRootObject, m_rootObject);
    return SLANG_OK;
}

Result ComputeCommandEncoderImpl::bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject)
{
    m_writer->setPipeline(state);
    Pipeline* pipelineImpl = static_cast<Pipeline*>(state);
    SLANG_RETURN_ON_FAIL(
        m_commandBuffer->m_device->createRootShaderObject(pipelineImpl->m_program, m_rootObject.writeRef())
    );
    m_rootObject->copyFrom(rootObject, m_commandBuffer->m_transientHeap);
    return SLANG_OK;
}

Result ComputeCommandEncoderImpl::dispatchCompute(int x, int y, int z)
{
    m_writer->bindRootShaderObject(m_rootObject);
    m_writer->dispatchCompute(x, y, z);
    return SLANG_OK;
}

Result ComputeCommandEncoderImpl::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    SLANG_RHI_UNIMPLEMENTED("dispatchComputeIndirect");
}

} // namespace rhi::cuda
