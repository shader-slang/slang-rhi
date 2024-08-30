#include "cuda-command-encoder.h"
#include "cuda-command-buffer.h"
#include "cuda-device.h"

namespace rhi::cuda {

void ResourceCommandEncoderImpl::init(CommandBufferImpl* cmdBuffer)
{
    m_writer = cmdBuffer;
}

SLANG_NO_THROW void SLANG_MCALL
ResourceCommandEncoderImpl::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    m_writer->copyBuffer(dst, dstOffset, src, srcOffset, size);
}

SLANG_NO_THROW void SLANG_MCALL
ResourceCommandEncoderImpl::uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data)
{
    m_writer->uploadBufferData(dst, offset, size, data);
}

SLANG_NO_THROW void SLANG_MCALL ResourceCommandEncoderImpl::writeTimestamp(IQueryPool* pool, GfxIndex index)
{
    m_writer->writeTimestamp(pool, index);
}

SLANG_NO_THROW void SLANG_MCALL ResourceCommandEncoderImpl::copyTexture(
    ITexture* dst,
    ResourceState dstState,
    SubresourceRange dstSubresource,
    ITexture::Offset3D dstOffset,
    ITexture* src,
    ResourceState srcState,
    SubresourceRange srcSubresource,
    ITexture::Offset3D srcOffset,
    ITexture::Extents extent
)
{
    SLANG_UNUSED(dst);
    SLANG_UNUSED(dstState);
    SLANG_UNUSED(dstSubresource);
    SLANG_UNUSED(dstOffset);
    SLANG_UNUSED(src);
    SLANG_UNUSED(srcState);
    SLANG_UNUSED(srcSubresource);
    SLANG_UNUSED(srcOffset);
    SLANG_UNUSED(extent);
    SLANG_RHI_UNIMPLEMENTED("copyTexture");
}

SLANG_NO_THROW void SLANG_MCALL ResourceCommandEncoderImpl::uploadTextureData(
    ITexture* dst,
    SubresourceRange subResourceRange,
    ITexture::Offset3D offset,
    ITexture::Extents extent,
    ITexture::SubresourceData* subResourceData,
    GfxCount subResourceDataCount
)
{
    SLANG_UNUSED(dst);
    SLANG_UNUSED(subResourceRange);
    SLANG_UNUSED(offset);
    SLANG_UNUSED(extent);
    SLANG_UNUSED(subResourceData);
    SLANG_UNUSED(subResourceDataCount);
    SLANG_RHI_UNIMPLEMENTED("uploadTextureData");
}

SLANG_NO_THROW void SLANG_MCALL ResourceCommandEncoderImpl::clearResourceView(
    IResourceView* view,
    ClearValue* clearValue,
    ClearResourceViewFlags::Enum flags
)
{
    SLANG_UNUSED(view);
    SLANG_UNUSED(clearValue);
    SLANG_UNUSED(flags);
    SLANG_RHI_UNIMPLEMENTED("clearResourceView");
}

SLANG_NO_THROW void SLANG_MCALL ResourceCommandEncoderImpl::resolveResource(
    ITexture* source,
    ResourceState sourceState,
    SubresourceRange sourceRange,
    ITexture* dest,
    ResourceState destState,
    SubresourceRange destRange
)
{
    SLANG_UNUSED(source);
    SLANG_UNUSED(sourceState);
    SLANG_UNUSED(sourceRange);
    SLANG_UNUSED(dest);
    SLANG_UNUSED(destState);
    SLANG_UNUSED(destRange);
    SLANG_RHI_UNIMPLEMENTED("resolveResource");
}

SLANG_NO_THROW void SLANG_MCALL ResourceCommandEncoderImpl::resolveQuery(
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

SLANG_NO_THROW void SLANG_MCALL ResourceCommandEncoderImpl::copyTextureToBuffer(
    IBuffer* dst,
    Offset dstOffset,
    Size dstSize,
    Size dstRowStride,
    ITexture* src,
    ResourceState srcState,
    SubresourceRange srcSubresource,
    ITexture::Offset3D srcOffset,
    ITexture::Extents extent
)
{
    SLANG_UNUSED(dst);
    SLANG_UNUSED(dstOffset);
    SLANG_UNUSED(dstSize);
    SLANG_UNUSED(dstRowStride);
    SLANG_UNUSED(src);
    SLANG_UNUSED(srcState);
    SLANG_UNUSED(srcSubresource);
    SLANG_UNUSED(srcOffset);
    SLANG_UNUSED(extent);
    SLANG_RHI_UNIMPLEMENTED("copyTextureToBuffer");
}

SLANG_NO_THROW void SLANG_MCALL ResourceCommandEncoderImpl::textureSubresourceBarrier(
    ITexture* texture,
    SubresourceRange subresourceRange,
    ResourceState src,
    ResourceState dst
)
{
    SLANG_UNUSED(texture);
    SLANG_UNUSED(subresourceRange);
    SLANG_UNUSED(src);
    SLANG_UNUSED(dst);
    SLANG_RHI_UNIMPLEMENTED("textureSubresourceBarrier");
}

SLANG_NO_THROW void SLANG_MCALL ResourceCommandEncoderImpl::beginDebugEvent(const char* name, float rgbColor[3])
{
    SLANG_UNUSED(name);
    SLANG_UNUSED(rgbColor);
}

void ComputeCommandEncoderImpl::init(CommandBufferImpl* cmdBuffer)
{
    m_writer = cmdBuffer;
    m_commandBuffer = cmdBuffer;
}

SLANG_NO_THROW Result SLANG_MCALL
ComputeCommandEncoderImpl::bindPipeline(IPipelineState* state, IShaderObject** outRootObject)
{
    m_writer->setPipelineState(state);
    PipelineStateBase* pipelineImpl = static_cast<PipelineStateBase*>(state);
    SLANG_RETURN_ON_FAIL(
        m_commandBuffer->m_device->createRootShaderObject(pipelineImpl->m_program, m_rootObject.writeRef())
    );
    returnComPtr(outRootObject, m_rootObject);
    return SLANG_OK;
}

SLANG_NO_THROW Result SLANG_MCALL
ComputeCommandEncoderImpl::bindPipelineWithRootObject(IPipelineState* state, IShaderObject* rootObject)
{
    m_writer->setPipelineState(state);
    PipelineStateBase* pipelineImpl = static_cast<PipelineStateBase*>(state);
    SLANG_RETURN_ON_FAIL(
        m_commandBuffer->m_device->createRootShaderObject(pipelineImpl->m_program, m_rootObject.writeRef())
    );
    m_rootObject->copyFrom(rootObject, m_commandBuffer->m_transientHeap);
    return SLANG_OK;
}

SLANG_NO_THROW Result SLANG_MCALL ComputeCommandEncoderImpl::dispatchCompute(int x, int y, int z)
{
    m_writer->bindRootShaderObject(m_rootObject);
    m_writer->dispatchCompute(x, y, z);
    return SLANG_OK;
}

SLANG_NO_THROW Result SLANG_MCALL ComputeCommandEncoderImpl::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    SLANG_RHI_UNIMPLEMENTED("dispatchComputeIndirect");
}

} // namespace rhi::cuda
