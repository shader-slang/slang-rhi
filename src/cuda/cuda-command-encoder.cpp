#include "cuda-command-encoder.h"
#include "cuda-command-buffer.h"
#include "cuda-device.h"

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
    Pipeline* pipelineImpl = static_cast<Pipeline*>(state);
    SLANG_RETURN_ON_FAIL(
        m_commandBuffer->m_device->createRootShaderObject(pipelineImpl->m_program, m_rootObject.writeRef())
    );
    returnComPtr(outRootObject, m_rootObject);
    return SLANG_OK;
}

Result ComputePassEncoderImpl::bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject)
{
    m_writer->setPipeline(state);
    Pipeline* pipelineImpl = static_cast<Pipeline*>(state);
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

} // namespace rhi::cuda
