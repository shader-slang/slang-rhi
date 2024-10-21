#include "d3d12-command-encoder.h"
#include "d3d12-command-buffer.h"
#include "d3d12-device.h"
#include "d3d12-helper-functions.h"
#include "d3d12-pipeline.h"
#include "d3d12-query.h"
#include "d3d12-shader-object.h"
#include "d3d12-shader-program.h"
#include "d3d12-shader-table.h"
#include "d3d12-texture.h"
#include "d3d12-transient-heap.h"
#include "d3d12-input-layout.h"
#include "d3d12-texture-view.h"
#include "d3d12-acceleration-structure.h"

#include "core/short_vector.h"

namespace rhi::d3d12 {

Result CommandEncoderImpl::init(DeviceImpl* device, CommandQueueImpl* queue, TransientResourceHeapImpl* transientHeap)
{
    m_device = device;
    m_transientHeap = transientHeap;
    if (!m_transientHeap)
    {
        SLANG_RETURN_ON_FAIL(m_device->createTransientResourceHeapImpl(
            ITransientResourceHeap::Flags::AllowResizing,
            4096,
            1024,
            1024,
            m_transientHeap.writeRef()
        ));
        m_transientHeap->breakStrongReferenceToDevice();
    }

    SLANG_RETURN_ON_FAIL(m_transientHeap->allocateCommandBuffer(m_commandBuffer.writeRef()));

    m_cmdList = static_cast<ID3D12GraphicsCommandList*>(m_commandBuffer->m_cmdList.get());
    m_cmdList->QueryInterface<ID3D12GraphicsCommandList1>(m_cmdList1.writeRef());
    m_cmdList->QueryInterface<ID3D12GraphicsCommandList4>(m_cmdList4.writeRef());
    m_cmdList->QueryInterface<ID3D12GraphicsCommandList6>(m_cmdList6.writeRef());

    return SLANG_OK;
}

Result CommandEncoderImpl::createRootShaderObject(ShaderProgram* program, ShaderObjectBase** outObject)
{
    RefPtr<RootShaderObjectImpl> object = new RootShaderObjectImpl();
    SLANG_RETURN_ON_FAIL(object->init(m_device));
    object->reset(m_device, checked_cast<ShaderProgramImpl*>(program)->m_rootObjectLayout, m_transientHeap);
    returnRefPtr(outObject, object);
    return SLANG_OK;
}

void CommandEncoderImpl::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    BufferImpl* dstBuffer = checked_cast<BufferImpl*>(dst);
    BufferImpl* srcBuffer = checked_cast<BufferImpl*>(src);

    requireBufferState(dstBuffer, ResourceState::CopyDestination);
    requireBufferState(srcBuffer, ResourceState::CopySource);
    commitBarriers();

    m_cmdList->CopyBufferRegion(
        dstBuffer->m_resource.getResource(),
        dstOffset,
        srcBuffer->m_resource.getResource(),
        srcOffset,
        size
    );
}

void CommandEncoderImpl::uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data)
{
    BufferImpl* dstBuffer = checked_cast<BufferImpl*>(dst);

    requireBufferState(dstBuffer, ResourceState::CopyDestination);
    commitBarriers();

    uploadBufferDataImpl(m_device->m_device, m_cmdList, m_transientHeap, dstBuffer, offset, size, data);
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
    TextureImpl* dstTexture = checked_cast<TextureImpl*>(dst);
    TextureImpl* srcTexture = checked_cast<TextureImpl*>(src);

    requireTextureState(dstTexture, dstSubresource, ResourceState::CopyDestination);
    requireTextureState(srcTexture, srcSubresource, ResourceState::CopySource);
    commitBarriers();

    if (dstSubresource.layerCount == 0 && dstSubresource.mipLevelCount == 0 && srcSubresource.layerCount == 0 &&
        srcSubresource.mipLevelCount == 0)
    {
        m_cmdList->CopyResource(dstTexture->m_resource.getResource(), srcTexture->m_resource.getResource());
        return;
    }

    DXGI_FORMAT d3dFormat = D3DUtil::getMapFormat(dstTexture->m_desc.format);
    uint32_t planeCount = D3DUtil::getPlaneSliceCount(d3dFormat);
    for (GfxIndex planeIndex = 0; planeIndex < planeCount; planeIndex++)
    {
        for (GfxIndex layer = 0; layer < dstSubresource.layerCount; layer++)
        {
            for (GfxIndex mipLevel = 0; mipLevel < dstSubresource.mipLevelCount; mipLevel++)
            {
                D3D12_TEXTURE_COPY_LOCATION dstRegion = {};

                dstRegion.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dstRegion.pResource = dstTexture->m_resource.getResource();
                dstRegion.SubresourceIndex = D3DUtil::getSubresourceIndex(
                    dstSubresource.mipLevel + mipLevel,
                    dstSubresource.baseArrayLayer + layer,
                    planeIndex,
                    dstTexture->m_desc.mipLevelCount,
                    dstTexture->m_desc.arrayLength
                );

                D3D12_TEXTURE_COPY_LOCATION srcRegion = {};
                srcRegion.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                srcRegion.pResource = srcTexture->m_resource.getResource();
                srcRegion.SubresourceIndex = D3DUtil::getSubresourceIndex(
                    srcSubresource.mipLevel + mipLevel,
                    srcSubresource.baseArrayLayer + layer,
                    planeIndex,
                    srcTexture->m_desc.mipLevelCount,
                    srcTexture->m_desc.arrayLength
                );

                D3D12_BOX srcBox = {};
                srcBox.left = srcOffset.x;
                srcBox.top = srcOffset.y;
                srcBox.front = srcOffset.z;
                srcBox.right = srcBox.left + extent.width;
                srcBox.bottom = srcBox.top + extent.height;
                srcBox.back = srcBox.front + extent.depth;

                m_cmdList->CopyTextureRegion(&dstRegion, dstOffset.x, dstOffset.y, dstOffset.z, &srcRegion, &srcBox);
            }
        }
    }
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
    TextureImpl* dstTexture = checked_cast<TextureImpl*>(dst);

    requireTextureState(dstTexture, subresourceRange, ResourceState::CopyDestination);
    commitBarriers();

    auto baseSubresourceIndex = D3DUtil::getSubresourceIndex(
        subresourceRange.mipLevel,
        subresourceRange.baseArrayLayer,
        0,
        dstTexture->m_desc.mipLevelCount,
        dstTexture->m_desc.arrayLength
    );
    auto textureSize = dstTexture->m_desc.size;
    const FormatInfo& formatInfo = getFormatInfo(dstTexture->m_desc.format);
    for (GfxCount i = 0; i < subresourceDataCount; i++)
    {
        auto subresourceIndex = baseSubresourceIndex + i;
        // Get the footprint
        D3D12_RESOURCE_DESC texDesc = dstTexture->m_resource.getResource()->GetDesc();

        D3D12_TEXTURE_COPY_LOCATION dstRegion = {};

        dstRegion.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstRegion.SubresourceIndex = subresourceIndex;
        dstRegion.pResource = dstTexture->m_resource.getResource();

        D3D12_TEXTURE_COPY_LOCATION srcRegion = {};
        srcRegion.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint = srcRegion.PlacedFootprint;
        footprint.Offset = 0;
        footprint.Footprint.Format = texDesc.Format;
        uint32_t mipLevel = D3DUtil::getSubresourceMipLevel(subresourceIndex, dstTexture->m_desc.mipLevelCount);
        if (extent.width != kRemainingTextureSize)
        {
            footprint.Footprint.Width = extent.width;
        }
        else
        {
            footprint.Footprint.Width = max(1, (textureSize.width >> mipLevel)) - offset.x;
        }
        if (extent.height != kRemainingTextureSize)
        {
            footprint.Footprint.Height = extent.height;
        }
        else
        {
            footprint.Footprint.Height = max(1, (textureSize.height >> mipLevel)) - offset.y;
        }
        if (extent.depth != kRemainingTextureSize)
        {
            footprint.Footprint.Depth = extent.depth;
        }
        else
        {
            footprint.Footprint.Depth = max(1, (textureSize.depth >> mipLevel)) - offset.z;
        }
        auto rowSize = (footprint.Footprint.Width + formatInfo.blockWidth - 1) / formatInfo.blockWidth *
                       formatInfo.blockSizeInBytes;
        auto rowCount = (footprint.Footprint.Height + formatInfo.blockHeight - 1) / formatInfo.blockHeight;
        footprint.Footprint.RowPitch =
            (UINT)D3DUtil::calcAligned(rowSize, (uint32_t)D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

        auto bufferSize = footprint.Footprint.RowPitch * rowCount * footprint.Footprint.Depth;

        IBuffer* stagingBuffer;
        Offset stagingBufferOffset = 0;
        m_transientHeap
            ->allocateStagingBuffer(bufferSize, stagingBuffer, stagingBufferOffset, MemoryType::Upload, true);
        SLANG_RHI_ASSERT(stagingBufferOffset == 0);
        BufferImpl* bufferImpl = checked_cast<BufferImpl*>(stagingBuffer);
        uint8_t* bufferData = nullptr;
        D3D12_RANGE mapRange = {0, 0};
        bufferImpl->m_resource.getResource()->Map(0, &mapRange, (void**)&bufferData);
        for (uint32_t z = 0; z < footprint.Footprint.Depth; z++)
        {
            auto imageStart = bufferData + footprint.Footprint.RowPitch * rowCount * (Size)z;
            auto srcData = (uint8_t*)subresourceData->data + subresourceData->strideZ * z;
            for (uint32_t row = 0; row < rowCount; row++)
            {
                memcpy(
                    imageStart + row * (Size)footprint.Footprint.RowPitch,
                    srcData + subresourceData->strideY * row,
                    rowSize
                );
            }
        }
        bufferImpl->m_resource.getResource()->Unmap(0, nullptr);
        srcRegion.pResource = bufferImpl->m_resource.getResource();
        m_cmdList->CopyTextureRegion(&dstRegion, offset.x, offset.y, offset.z, &srcRegion, nullptr);
    }
}

void CommandEncoderImpl::clearBuffer(IBuffer* buffer, const BufferRange* range)
{
    // TODO implement
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
    // TODO implement
    SLANG_UNUSED(texture);
    SLANG_UNUSED(clearValue);
    SLANG_UNUSED(subresourceRange);
    SLANG_UNUSED(clearDepth);
    SLANG_UNUSED(clearStencil);
    SLANG_RHI_UNIMPLEMENTED("clearTexture");
}

void CommandEncoderImpl::resolveQuery(
    IQueryPool* queryPool,
    GfxIndex index,
    GfxCount count,
    IBuffer* buffer,
    Offset offset
)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);

    requireBufferState(bufferImpl, ResourceState::CopyDestination);
    commitBarriers();

    auto queryBase = checked_cast<QueryPool*>(queryPool);
    switch (queryBase->m_desc.type)
    {
    case QueryType::AccelerationStructureCompactedSize:
    case QueryType::AccelerationStructureCurrentSize:
    case QueryType::AccelerationStructureSerializedSize:
    {
        auto queryPoolImpl = checked_cast<PlainBufferProxyQueryPoolImpl*>(queryPool);
        auto srcQueryBuffer = queryPoolImpl->m_buffer->m_resource.getResource();

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.pResource = srcQueryBuffer;
        m_cmdList->ResourceBarrier(1, &barrier);

        m_cmdList->CopyBufferRegion(
            bufferImpl->m_resource.getResource(),
            (uint64_t)offset,
            srcQueryBuffer,
            index * sizeof(uint64_t),
            count * sizeof(uint64_t)
        );

        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.pResource = srcQueryBuffer;
        m_cmdList->ResourceBarrier(1, &barrier);
    }
    break;
    default:
    {
        auto queryPoolImpl = checked_cast<QueryPoolImpl*>(queryPool);
        auto bufferImpl = checked_cast<BufferImpl*>(buffer);
        m_cmdList->ResolveQueryData(
            queryPoolImpl->m_queryHeap.get(),
            queryPoolImpl->m_queryType,
            index,
            count,
            bufferImpl->m_resource.getResource(),
            offset
        );
    }
    break;
    }
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
    SLANG_RHI_ASSERT(srcSubresource.mipLevelCount <= 1);

    BufferImpl* dstBuffer = checked_cast<BufferImpl*>(dst);
    TextureImpl* srcTexture = checked_cast<TextureImpl*>(src);

    requireBufferState(dstBuffer, ResourceState::CopyDestination);
    requireTextureState(srcTexture, srcSubresource, ResourceState::CopySource);
    commitBarriers();

    auto baseSubresourceIndex = D3DUtil::getSubresourceIndex(
        srcSubresource.mipLevel,
        srcSubresource.baseArrayLayer,
        0,
        srcTexture->m_desc.mipLevelCount,
        srcTexture->m_desc.arrayLength
    );
    auto textureSize = srcTexture->m_desc.size;
    const FormatInfo& formatInfo = getFormatInfo(srcTexture->m_desc.format);
    if (srcSubresource.mipLevelCount == 0)
        srcSubresource.mipLevelCount = srcTexture->m_desc.mipLevelCount;
    if (srcSubresource.layerCount == 0)
        srcSubresource.layerCount = srcTexture->m_desc.arrayLength;

    for (GfxCount layer = 0; layer < srcSubresource.layerCount; layer++)
    {
        // Get the footprint
        D3D12_RESOURCE_DESC texDesc = srcTexture->m_resource.getResource()->GetDesc();

        D3D12_TEXTURE_COPY_LOCATION dstRegion = {};
        dstRegion.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstRegion.pResource = dstBuffer->m_resource.getResource();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint = dstRegion.PlacedFootprint;

        D3D12_TEXTURE_COPY_LOCATION srcRegion = {};
        srcRegion.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcRegion.SubresourceIndex = D3DUtil::getSubresourceIndex(
            srcSubresource.mipLevel,
            layer + srcSubresource.baseArrayLayer,
            0,
            srcTexture->m_desc.mipLevelCount,
            srcTexture->m_desc.arrayLength
        );
        srcRegion.pResource = srcTexture->m_resource.getResource();

        footprint.Offset = dstOffset;
        footprint.Footprint.Format = texDesc.Format;
        uint32_t mipLevel = srcSubresource.mipLevel;
        if (extent.width != 0xFFFFFFFF)
        {
            footprint.Footprint.Width = extent.width;
        }
        else
        {
            footprint.Footprint.Width = max(1, (textureSize.width >> mipLevel)) - srcOffset.x;
        }
        if (extent.height != 0xFFFFFFFF)
        {
            footprint.Footprint.Height = extent.height;
        }
        else
        {
            footprint.Footprint.Height = max(1, (textureSize.height >> mipLevel)) - srcOffset.y;
        }
        if (extent.depth != 0xFFFFFFFF)
        {
            footprint.Footprint.Depth = extent.depth;
        }
        else
        {
            footprint.Footprint.Depth = max(1, (textureSize.depth >> mipLevel)) - srcOffset.z;
        }

        SLANG_RHI_ASSERT(dstRowStride % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT == 0);
        footprint.Footprint.RowPitch = (UINT)dstRowStride;

        auto bufferSize = footprint.Footprint.RowPitch * footprint.Footprint.Height * footprint.Footprint.Depth;

        D3D12_BOX srcBox = {};
        srcBox.left = srcOffset.x;
        srcBox.top = srcOffset.y;
        srcBox.front = srcOffset.z;
        srcBox.right = srcOffset.x + extent.width;
        srcBox.bottom = srcOffset.y + extent.height;
        srcBox.back = srcOffset.z + extent.depth;
        m_cmdList->CopyTextureRegion(&dstRegion, 0, 0, 0, &srcRegion, &srcBox);
    }
}

void CommandEncoderImpl::beginRenderPass(const RenderPassDesc& desc)
{
    m_renderTargetViews.resize(desc.colorAttachmentCount);
    m_resolveTargetViews.resize(desc.colorAttachmentCount);
    short_vector<D3D12_CPU_DESCRIPTOR_HANDLE> renderTargetDescriptors;
    for (Index i = 0; i < desc.colorAttachmentCount; i++)
    {
        m_renderTargetViews[i] = checked_cast<TextureViewImpl*>(desc.colorAttachments[i].view);
        m_resolveTargetViews[i] = checked_cast<TextureViewImpl*>(desc.colorAttachments[i].resolveTarget);
        requireTextureState(
            m_renderTargetViews[i]->m_texture,
            m_renderTargetViews[i]->m_desc.subresourceRange,
            ResourceState::RenderTarget
        );
        renderTargetDescriptors.push_back(m_renderTargetViews[i]->getRTV().cpuHandle);
    }
    if (desc.depthStencilAttachment)
    {
        m_depthStencilView = checked_cast<TextureViewImpl*>(desc.depthStencilAttachment->view);
        requireTextureState(
            m_depthStencilView->m_texture,
            m_depthStencilView->m_desc.subresourceRange,
            desc.depthStencilAttachment->depthReadOnly ? ResourceState::DepthRead : ResourceState::DepthWrite
        );
    }

    commitBarriers();

    m_cmdList->OMSetRenderTargets(
        (UINT)m_renderTargetViews.size(),
        renderTargetDescriptors.data(),
        FALSE,
        m_depthStencilView ? &m_depthStencilView->getDSV().cpuHandle : nullptr
    );

    // Issue clear commands based on render pass set up.
    for (Index i = 0; i < m_renderTargetViews.size(); i++)
    {
        const auto& attachment = desc.colorAttachments[i];
        if (attachment.loadOp == LoadOp::Clear)
        {
            m_cmdList->ClearRenderTargetView(renderTargetDescriptors[i], attachment.clearValue, 0, nullptr);
        }
    }

    if (desc.depthStencilAttachment)
    {
        const auto& attachment = *desc.depthStencilAttachment;
        uint32_t clearFlags = 0;
        if (attachment.depthLoadOp == LoadOp::Clear)
        {
            clearFlags |= D3D12_CLEAR_FLAG_DEPTH;
        }
        if (attachment.stencilLoadOp == LoadOp::Clear)
        {
            clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
        }
        if (clearFlags)
        {
            m_cmdList->ClearDepthStencilView(
                m_depthStencilView->getDSV().cpuHandle,
                (D3D12_CLEAR_FLAGS)clearFlags,
                attachment.depthClearValue,
                attachment.stencilClearValue,
                0,
                nullptr
            );
        }
    }

    m_renderPassActive = true;
}

void CommandEncoderImpl::endRenderPass()
{
    bool needsResolve = false;
    for (size_t i = 0; i < m_renderTargetViews.size(); ++i)
    {
        if (m_renderTargetViews[i] && m_resolveTargetViews[i])
        {
            requireTextureState(
                m_renderTargetViews[i]->m_texture,
                m_renderTargetViews[i]->m_desc.subresourceRange,
                ResourceState::ResolveSource
            );
            requireTextureState(
                m_resolveTargetViews[i]->m_texture,
                m_resolveTargetViews[i]->m_desc.subresourceRange,
                ResourceState::ResolveDestination
            );
            needsResolve = true;
        }
    }

    if (needsResolve)
    {
        commitBarriers();

        for (size_t i = 0; i < m_renderTargetViews.size(); ++i)
        {
            if (m_renderTargetViews[i] && m_resolveTargetViews[i])
            {
                TextureViewImpl* srcView = m_renderTargetViews[i].get();
                TextureViewImpl* dstView = m_resolveTargetViews[i].get();
                DXGI_FORMAT format = D3DUtil::getMapFormat(srcView->m_texture->m_desc.format);
                m_cmdList->ResolveSubresource(
                    dstView->m_texture->m_resource.getResource(),
                    0, // TODO iterate subresources
                    srcView->m_texture->m_resource.getResource(),
                    0, // TODO iterate subresources
                    format
                );
            }
        }
    }

    m_renderTargetViews.clear();
    m_resolveTargetViews.clear();
    m_depthStencilView = nullptr;

    m_renderPassActive = false;
}

template<typename T>
inline bool arraysEqual(GfxCount countA, GfxCount countB, const T* a, const T* b)
{
    return (countA == countB) ? std::memcmp(a, b, countA * sizeof(T)) == 0 : false;
}

void CommandEncoderImpl::setRenderState(const RenderState& state)
{
    if (!m_renderPassActive)
        return;

    bool updatePipeline = !m_renderStateValid || state.pipeline != m_renderState.pipeline;
    bool updateRootObject = updatePipeline || state.rootObject != m_renderState.rootObject;
    bool updateStencilRef = !m_renderStateValid || state.stencilRef != m_renderState.stencilRef;
    bool updateVertexBuffers = !m_renderStateValid || !arraysEqual(
                                                          state.vertexBufferCount,
                                                          m_renderState.vertexBufferCount,
                                                          state.vertexBuffers,
                                                          m_renderState.vertexBuffers
                                                      );
    bool updateIndexBuffer = !m_renderStateValid || state.indexFormat != m_renderState.indexFormat ||
                             state.indexBuffer != m_renderState.indexBuffer;
    bool updateViewports =
        !m_renderStateValid ||
        !arraysEqual(state.viewportCount, m_renderState.viewportCount, state.viewports, m_renderState.viewports);
    bool updateScissorRects = !m_renderStateValid || !arraysEqual(
                                                         state.scissorRectCount,
                                                         m_renderState.scissorRectCount,
                                                         state.scissorRects,
                                                         m_renderState.scissorRects
                                                     );

    if (updatePipeline)
    {
        m_renderPipeline = checked_cast<RenderPipelineImpl*>(state.pipeline);
        m_cmdList->SetGraphicsRootSignature(m_renderPipeline->m_rootObjectLayout->m_rootSignature);
        m_cmdList->SetPipelineState(m_renderPipeline->m_pipelineState);
        m_cmdList->IASetPrimitiveTopology(m_renderPipeline->m_primitiveTopology);
    }

    if (updateRootObject)
    {
        m_rootObject = checked_cast<RootShaderObjectImpl*>(state.rootObject);
        GraphicsSubmitter submitter(m_device->m_device, m_cmdList, m_transientHeap);
        if (bindRootObject(m_rootObject, m_renderPipeline->m_rootObjectLayout, &submitter) != SLANG_OK)
        {
            // TODO issue a message?
            return;
        }
    }

    // TODO support setting sample positions
#if 0
    if (updateSamplePositions)
    {
        m_commandBuffer->m_cmdList1->SetSamplePositions(
            (uint32_t)samplesPerPixel,
            (uint32_t)pixelCount,
            (D3D12_SAMPLE_POSITION*)samplePositions
        );
    }
#endif

    if (updateStencilRef)
    {
        m_cmdList->OMSetStencilRef((UINT)state.stencilRef);
    }

    if (updateVertexBuffers)
    {
        D3D12_VERTEX_BUFFER_VIEW vertexViews[SLANG_COUNT_OF(state.vertexBuffers)];
        for (Index i = 0; i < state.vertexBufferCount; ++i)
        {
            BufferImpl* buffer = checked_cast<BufferImpl*>(state.vertexBuffers[i].buffer);
            Offset offset = state.vertexBuffers[i].offset;
            requireBufferState(buffer, ResourceState::VertexBuffer);

            D3D12_VERTEX_BUFFER_VIEW& vertexView = vertexViews[i];
            vertexView.BufferLocation = buffer->m_resource.getResource()->GetGPUVirtualAddress() + offset;
            vertexView.SizeInBytes = UINT(buffer->m_desc.size - offset);
            vertexView.StrideInBytes = m_renderPipeline->m_inputLayout->m_vertexStreamStrides[i];
        }
        m_cmdList->IASetVertexBuffers(0, (UINT)state.vertexBufferCount, vertexViews);
    }

    if (updateIndexBuffer)
    {
        if (state.indexBuffer.buffer)
        {
            BufferImpl* buffer = checked_cast<BufferImpl*>(state.indexBuffer.buffer);
            Offset offset = state.indexBuffer.offset;
            requireBufferState(buffer, ResourceState::IndexBuffer);

            D3D12_INDEX_BUFFER_VIEW indexBufferView;
            indexBufferView.BufferLocation = buffer->m_resource.getResource()->GetGPUVirtualAddress() + offset;
            indexBufferView.SizeInBytes = UINT(buffer->m_desc.size - offset);
            indexBufferView.Format = D3DUtil::getIndexFormat(state.indexFormat);
            m_cmdList->IASetIndexBuffer(&indexBufferView);
        }
        else
        {
            m_cmdList->IASetIndexBuffer(nullptr);
        }
    }

    if (updateViewports)
    {
        static const int kMaxViewports = D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        SLANG_RHI_ASSERT(state.viewportCount <= kMaxViewports);
        D3D12_VIEWPORT viewports[SLANG_COUNT_OF(state.viewports)];
        for (GfxIndex i = 0; i < state.viewportCount; ++i)
        {
            const Viewport& src = state.viewports[i];
            D3D12_VIEWPORT& dst = viewports[i];
            dst.TopLeftX = src.originX;
            dst.TopLeftY = src.originY;
            dst.Width = src.extentX;
            dst.Height = src.extentY;
            dst.MinDepth = src.minZ;
            dst.MaxDepth = src.maxZ;
        }
        m_cmdList->RSSetViewports((UINT)state.viewportCount, viewports);
    }

    if (updateScissorRects)
    {
        static const int kMaxScissorRects = D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        SLANG_RHI_ASSERT(state.scissorRectCount <= kMaxScissorRects);
        D3D12_RECT scissorRects[SLANG_COUNT_OF(state.scissorRects)];
        for (GfxIndex i = 0; i < state.scissorRectCount; ++i)
        {
            const ScissorRect& src = state.scissorRects[i];
            D3D12_RECT& dst = scissorRects[i];
            dst.left = LONG(src.minX);
            dst.top = LONG(src.minY);
            dst.right = LONG(src.maxX);
            dst.bottom = LONG(src.maxY);
        }
        m_cmdList->RSSetScissorRects((UINT)state.scissorRectCount, scissorRects);
    }

    m_renderStateValid = true;
    m_renderState = state;

    m_computeStateValid = false;
    m_computeState = {};
    m_computePipeline = nullptr;

    m_rayTracingStateValid = false;
    m_rayTracingState = {};
    m_rayTracingPipeline = nullptr;
}

void CommandEncoderImpl::draw(const DrawArguments& args)
{
    if (!m_renderStateValid)
        return;

    m_cmdList->DrawInstanced(
        (UINT)args.vertexCount,
        (UINT)args.instanceCount,
        (UINT)args.startIndexLocation,
        (UINT)args.startInstanceLocation
    );
}

void CommandEncoderImpl::drawIndexed(const DrawArguments& args)
{
    if (!m_renderStateValid)
        return;

    m_cmdList->DrawIndexedInstanced(
        (UINT)args.vertexCount,
        (UINT)args.instanceCount,
        (UINT)args.startIndexLocation,
        (UINT)args.startVertexLocation,
        (UINT)args.startInstanceLocation
    );
}

void CommandEncoderImpl::drawIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    if (!m_renderStateValid)
        return;

    BufferImpl* argBufferImpl = checked_cast<BufferImpl*>(argBuffer);
    BufferImpl* countBufferImpl = checked_cast<BufferImpl*>(countBuffer);

    requireBufferState(argBufferImpl, ResourceState::IndirectArgument);
    if (countBufferImpl)
    {
        requireBufferState(countBufferImpl, ResourceState::IndirectArgument);
    }

    m_cmdList->ExecuteIndirect(
        m_device->drawIndirectCmdSignature,
        (UINT)maxDrawCount,
        argBufferImpl->m_resource,
        (UINT64)argOffset,
        countBufferImpl ? countBufferImpl->m_resource.getResource() : nullptr,
        (UINT64)countOffset
    );
}

void CommandEncoderImpl::drawIndexedIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    if (!m_renderStateValid)
        return;

    BufferImpl* argBufferImpl = checked_cast<BufferImpl*>(argBuffer);
    BufferImpl* countBufferImpl = checked_cast<BufferImpl*>(countBuffer);

    requireBufferState(argBufferImpl, ResourceState::IndirectArgument);
    if (countBufferImpl)
    {
        requireBufferState(countBufferImpl, ResourceState::IndirectArgument);
    }

    m_cmdList->ExecuteIndirect(
        m_device->drawIndexedIndirectCmdSignature,
        (UINT)maxDrawCount,
        argBufferImpl->m_resource,
        (UINT64)argOffset,
        countBufferImpl ? countBufferImpl->m_resource.getResource() : nullptr,
        (UINT64)countOffset
    );
}

void CommandEncoderImpl::drawMeshTasks(int x, int y, int z)
{
    m_cmdList6->DispatchMesh(x, y, z);
}

void CommandEncoderImpl::setComputeState(const ComputeState& state)
{
    if (m_renderPassActive)
        return;

    bool updatePipeline = !m_computeStateValid || state.pipeline != m_computeState.pipeline;
    bool updateRootObject = updatePipeline || state.rootObject != m_computeState.rootObject;

    if (updatePipeline)
    {
        m_computePipeline = checked_cast<ComputePipelineImpl*>(state.pipeline);
        m_cmdList->SetComputeRootSignature(m_computePipeline->m_rootObjectLayout->m_rootSignature);
        m_cmdList->SetPipelineState(m_computePipeline->m_pipelineState);
    }

    if (updateRootObject)
    {
        m_rootObject = checked_cast<RootShaderObjectImpl*>(state.rootObject);
        ComputeSubmitter submitter(m_device->m_device, m_cmdList, m_transientHeap);
        if (bindRootObject(m_rootObject, m_computePipeline->m_rootObjectLayout, &submitter) != SLANG_OK)
        {
            // TODO issue a message?
            return;
        }
    }

    m_computeStateValid = true;
    m_computeState = state;
}

void CommandEncoderImpl::dispatchCompute(int x, int y, int z)
{
    if (!m_computeStateValid)
        return;

    m_cmdList->Dispatch(x, y, z);
}

void CommandEncoderImpl::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    if (!m_computeStateValid)
        return;

    BufferImpl* argBufferImpl = checked_cast<BufferImpl*>(argBuffer);

    requireBufferState(argBufferImpl, ResourceState::IndirectArgument);
    commitBarriers();

    m_cmdList->ExecuteIndirect(
        m_device->dispatchIndirectCmdSignature,
        (UINT)1,
        argBufferImpl->m_resource,
        (UINT64)offset,
        nullptr,
        0
    );
}

#if SLANG_RHI_DXR

void CommandEncoderImpl::setRayTracingState(const RayTracingState& state)
{
    if (m_renderPassActive)
        return;

    bool updatePipeline = !m_rayTracingStateValid || state.pipeline != m_rayTracingState.pipeline;
    bool updateRootObject = updatePipeline || state.rootObject != m_rayTracingState.rootObject;
    bool updateShaderTable = updatePipeline || state.shaderTable != m_rayTracingState.shaderTable;

    if (updatePipeline)
    {
        m_rayTracingPipeline = checked_cast<RayTracingPipelineImpl*>(state.pipeline);
        m_cmdList->SetComputeRootSignature(m_rayTracingPipeline->m_rootObjectLayout->m_rootSignature);
        m_cmdList4->SetPipelineState1(m_rayTracingPipeline->m_stateObject);
    }

    if (updateRootObject)
    {
        m_rootObject = checked_cast<RootShaderObjectImpl*>(state.rootObject);
        ComputeSubmitter submitter(m_device->m_device, m_cmdList, m_transientHeap);
        if (bindRootObject(m_rootObject, m_rayTracingPipeline->m_rootObjectLayout, &submitter) != SLANG_OK)
        {
            // TODO issue a message?
            return;
        }
    }

    if (updateShaderTable)
    {
        m_shaderTable = checked_cast<ShaderTableImpl*>(state.shaderTable);

        Buffer* shaderTableBuffer = m_shaderTable->getOrCreateBuffer(m_rayTracingPipeline, m_transientHeap, this);
        DeviceAddress shaderTableAddr = shaderTableBuffer->getDeviceAddress();

        m_dispatchRaysDesc = {};

        // Raygen index is set at dispatch time.
        m_rayGenTableAddr = shaderTableAddr + m_shaderTable->m_rayGenTableOffset;
        m_dispatchRaysDesc.RayGenerationShaderRecord.StartAddress = shaderTableAddr;
        m_dispatchRaysDesc.RayGenerationShaderRecord.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

        if (m_shaderTable->m_missShaderCount > 0)
        {
            m_dispatchRaysDesc.MissShaderTable.StartAddress = shaderTableAddr + m_shaderTable->m_missTableOffset;
            m_dispatchRaysDesc.MissShaderTable.SizeInBytes =
                m_shaderTable->m_missShaderCount * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
            m_dispatchRaysDesc.MissShaderTable.StrideInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        }

        if (m_shaderTable->m_hitGroupCount > 0)
        {
            m_dispatchRaysDesc.HitGroupTable.StartAddress = shaderTableAddr + m_shaderTable->m_hitGroupTableOffset;
            m_dispatchRaysDesc.HitGroupTable.SizeInBytes =
                m_shaderTable->m_hitGroupCount * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
            m_dispatchRaysDesc.HitGroupTable.StrideInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        }

        if (m_shaderTable->m_callableShaderCount > 0)
        {
            m_dispatchRaysDesc.CallableShaderTable.StartAddress =
                shaderTableAddr + m_shaderTable->m_callableTableOffset;
            m_dispatchRaysDesc.CallableShaderTable.SizeInBytes =
                m_shaderTable->m_callableShaderCount * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
            m_dispatchRaysDesc.CallableShaderTable.StrideInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        }
    }

    m_rayTracingStateValid = true;
    m_rayTracingState = state;
}

void CommandEncoderImpl::dispatchRays(GfxIndex raygenShaderIndex, GfxCount width, GfxCount height, GfxCount depth)
{
    if (!m_rayTracingStateValid)
        return;

#if 0
    RefPtr<Pipeline> newPipeline;
    Pipeline* pipeline = m_currentPipeline.Ptr();
    {
        struct RayTracingSubmitter : public ComputeSubmitter
        {
            ID3D12GraphicsCommandList4* m_cmdList4;
            RayTracingSubmitter(ID3D12GraphicsCommandList4* cmdList4)
                : ComputeSubmitter(cmdList4)
                , m_cmdList4(cmdList4)
            {
            }
            virtual void setPipeline(Pipeline* pipeline) override
            {
                m_cmdList4->SetPipelineState1(
                    checked_cast<RayTracingPipelineImpl*>(pipeline->m_rayTracingPipeline.get())->m_stateObject.get()
                );
            }
        };
        RayTracingSubmitter submitter(m_commandBuffer->m_cmdList4);
        SLANG_RETURN_ON_FAIL(_bindRenderState(&submitter, newPipeline));
        if (newPipeline)
            pipeline = newPipeline.Ptr();
    }

    RayTracingPipelineImpl* rayTracingPipeline =
        checked_cast<RayTracingPipelineImpl*>(pipeline->m_rayTracingPipeline.get());
#endif

    m_dispatchRaysDesc.RayGenerationShaderRecord.StartAddress =
        m_rayGenTableAddr + raygenShaderIndex * kRayGenRecordSize;
    m_dispatchRaysDesc.Width = (UINT)width;
    m_dispatchRaysDesc.Height = (UINT)height;
    m_dispatchRaysDesc.Depth = (UINT)depth;
    m_cmdList4->DispatchRays(&m_dispatchRaysDesc);
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
    AccelerationStructureImpl* dstImpl = checked_cast<AccelerationStructureImpl*>(dst);
    AccelerationStructureImpl* srcImpl = checked_cast<AccelerationStructureImpl*>(src);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.DestAccelerationStructureData = dstImpl->getDeviceAddress();
    buildDesc.SourceAccelerationStructureData = srcImpl ? srcImpl->getDeviceAddress() : 0;
    buildDesc.ScratchAccelerationStructureData = scratchBuffer.buffer->getDeviceAddress() + scratchBuffer.offset;
    AccelerationStructureInputsBuilder builder;
    builder.build(desc, m_device->m_debugCallback);
    buildDesc.Inputs = builder.desc;

    std::vector<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC> postBuildInfoDescs;
    translatePostBuildInfoDescs(propertyQueryCount, queryDescs, postBuildInfoDescs);
    m_cmdList4->BuildRaytracingAccelerationStructure(&buildDesc, (UINT)propertyQueryCount, postBuildInfoDescs.data());
}

void CommandEncoderImpl::copyAccelerationStructure(
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    AccelerationStructureCopyMode mode
)
{
    auto dstImpl = checked_cast<AccelerationStructureImpl*>(dst);
    auto srcImpl = checked_cast<AccelerationStructureImpl*>(src);
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE copyMode;
    switch (mode)
    {
    case AccelerationStructureCopyMode::Clone:
        copyMode = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_CLONE;
        break;
    case AccelerationStructureCopyMode::Compact:
        copyMode = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT;
        break;
    default:
        m_device->handleMessage(
            DebugMessageType::Error,
            DebugMessageSource::Layer,
            "Unsupported AccelerationStructureCopyMode."
        );
        return;
    }
    m_cmdList4->CopyRaytracingAccelerationStructure(dstImpl->getDeviceAddress(), srcImpl->getDeviceAddress(), copyMode);
}

void CommandEncoderImpl::queryAccelerationStructureProperties(
    GfxCount accelerationStructureCount,
    IAccelerationStructure* const* accelerationStructures,
    GfxCount queryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    std::vector<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC> postBuildInfoDescs;
    std::vector<DeviceAddress> asAddresses;
    asAddresses.resize(accelerationStructureCount);
    for (GfxIndex i = 0; i < accelerationStructureCount; i++)
        asAddresses[i] = accelerationStructures[i]->getDeviceAddress();
    translatePostBuildInfoDescs(queryCount, queryDescs, postBuildInfoDescs);
    m_cmdList4->EmitRaytracingAccelerationStructurePostbuildInfo(
        postBuildInfoDescs.data(),
        (UINT)accelerationStructureCount,
        asAddresses.data()
    );
}

void CommandEncoderImpl::serializeAccelerationStructure(BufferWithOffset dst, IAccelerationStructure* src)
{
    auto srcImpl = checked_cast<AccelerationStructureImpl*>(src);
    m_cmdList4->CopyRaytracingAccelerationStructure(
        checked_cast<BufferImpl*>(dst.buffer)->getDeviceAddress() + dst.offset,
        srcImpl->getDeviceAddress(),
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_SERIALIZE
    );
}

void CommandEncoderImpl::deserializeAccelerationStructure(IAccelerationStructure* dst, BufferWithOffset src)
{
    auto dstImpl = checked_cast<AccelerationStructureImpl*>(dst);
    m_cmdList4->CopyRaytracingAccelerationStructure(
        dstImpl->getDeviceAddress(),
        checked_cast<BufferImpl*>(src.buffer)->getDeviceAddress() + src.offset,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_DESERIALIZE
    );
}

#else // SLANG_RHI_DXR

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
    SLANG_RHI_UNIMPLEMENTED("dispatchRays");
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

#endif // SLANG_RHI_DXR

void CommandEncoderImpl::setBufferState(IBuffer* buffer, ResourceState state)
{
    m_stateTracking.setBufferState(checked_cast<BufferImpl*>(buffer), state);
}

void CommandEncoderImpl::setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state)
{
    m_stateTracking.setTextureState(checked_cast<TextureImpl*>(texture), subresourceRange, state);
}

void CommandEncoderImpl::beginDebugEvent(const char* name, float rgbColor[3])
{
    auto beginEvent = m_device->m_BeginEventOnCommandList;
    if (beginEvent)
    {
        UINT64 color = 0xff000000 | (uint8_t(rgbColor[0] * 255.0f) << 16) | (uint8_t(rgbColor[1] * 255.0f) << 8) |
                       uint8_t(rgbColor[2] * 255.0f);
        beginEvent(m_cmdList, color, name);
    }
}

void CommandEncoderImpl::endDebugEvent()
{
    auto endEvent = m_device->m_EndEventOnCommandList;
    if (endEvent)
    {
        endEvent(m_cmdList);
    }
}

void CommandEncoderImpl::writeTimestamp(IQueryPool* pool, GfxIndex index)
{
    checked_cast<QueryPoolImpl*>(pool)->writeTimestamp(m_cmdList, index);
}

Result CommandEncoderImpl::finish(ICommandBuffer** outCommandBuffer)
{
    if (!m_commandBuffer)
    {
        return SLANG_FAIL;
    }
    // endPassEncoder();

    // Transition all resources back to their default states.
    m_stateTracking.requireDefaultStates();
    commitBarriers();
    m_stateTracking.clear();

    SLANG_RETURN_ON_FAIL(m_cmdList->Close());
    returnComPtr(outCommandBuffer, m_commandBuffer);
    m_commandBuffer = nullptr;
    m_cmdList = nullptr;
    m_cmdList1 = nullptr;
    m_cmdList4 = nullptr;
    m_cmdList6 = nullptr;
    return SLANG_OK;
}

Result CommandEncoderImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12GraphicsCommandList;
    outHandle->value = (uint64_t)m_cmdList.get();
    return SLANG_OK;
}

void CommandEncoderImpl::requireBufferState(BufferImpl* buffer, ResourceState state)
{
    m_stateTracking.setBufferState(buffer, state);
}

void CommandEncoderImpl::requireTextureState(
    TextureImpl* texture,
    SubresourceRange subresourceRange,
    ResourceState state
)
{
    m_stateTracking.setTextureState(texture, subresourceRange, state);
}

void CommandEncoderImpl::commitBarriers()
{
    short_vector<D3D12_RESOURCE_BARRIER, 16> barriers;

    for (const auto& bufferBarrier : m_stateTracking.getBufferBarriers())
    {
        BufferImpl* buffer = checked_cast<BufferImpl*>(bufferBarrier.buffer);
        D3D12_RESOURCE_BARRIER barrier = {};
        bool isUAVBarrier =
            (bufferBarrier.stateBefore == bufferBarrier.stateAfter &&
             bufferBarrier.stateAfter == ResourceState::UnorderedAccess);
        if (isUAVBarrier)
        {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = buffer->m_resource;
        }
        else
        {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = buffer->m_resource;
            barrier.Transition.StateBefore = D3DUtil::getResourceState(bufferBarrier.stateBefore);
            barrier.Transition.StateAfter = D3DUtil::getResourceState(bufferBarrier.stateAfter);
            barrier.Transition.Subresource = 0;
        }
        barriers.push_back(barrier);
    }

    for (const auto& textureBarrier : m_stateTracking.getTextureBarriers())
    {
        TextureImpl* texture = checked_cast<TextureImpl*>(textureBarrier.texture);
        D3D12_RESOURCE_BARRIER barrier = {};
        if (textureBarrier.entireTexture)
        {
            bool isUAVBarrier =
                (textureBarrier.stateBefore == textureBarrier.stateAfter &&
                 textureBarrier.stateAfter == ResourceState::UnorderedAccess);
            if (isUAVBarrier)
            {
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.UAV.pResource = texture->m_resource;
            }
            else
            {
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = texture->m_resource;
                barrier.Transition.StateBefore = D3DUtil::getResourceState(textureBarrier.stateBefore);
                barrier.Transition.StateAfter = D3DUtil::getResourceState(textureBarrier.stateAfter);
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            }
            barriers.push_back(barrier);
        }
        else
        {
            uint32_t mipLevelCount = texture->m_desc.mipLevelCount;
            uint32_t arrayLayerCount =
                texture->m_desc.arrayLength * (texture->m_desc.type == TextureType::TextureCube ? 6 : 1);
            DXGI_FORMAT d3dFormat = D3DUtil::getMapFormat(texture->m_desc.format);
            uint32_t planeCount = D3DUtil::getPlaneSliceCount(d3dFormat);
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = texture->m_resource;
            barrier.Transition.StateBefore = D3DUtil::getResourceState(textureBarrier.stateBefore);
            barrier.Transition.StateAfter = D3DUtil::getResourceState(textureBarrier.stateAfter);
            for (uint32_t planeIndex = 0; planeIndex < planeCount; ++planeIndex)
            {
                barrier.Transition.Subresource = D3DUtil::getSubresourceIndex(
                    textureBarrier.mipLevel,
                    textureBarrier.arrayLayer,
                    planeIndex,
                    mipLevelCount,
                    arrayLayerCount
                );
                barriers.push_back(barrier);
            }
        }
    }

    if (!barriers.empty())
    {
        m_cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
    }

    m_stateTracking.clearBarriers();
}

Result CommandEncoderImpl::bindRootObject(
    RootShaderObjectImpl* rootObject,
    RootShaderObjectLayoutImpl* rootObjectLayout,
    Submitter* submitter
)
{
    // We need to set up a context for binding shader objects to the pipeline state.
    // This type mostly exists to bundle together a bunch of parameters that would
    // otherwise need to be tunneled down through all the shader object binding
    // logic.
    //
    BindingContext context = {};
    context.submitter = submitter;
    context.device = m_device;
    context.transientHeap = m_transientHeap;
    context.outOfMemoryHeap = (D3D12_DESCRIPTOR_HEAP_TYPE)(-1);

    // Transition all resources to the appropriate state and commit the barriers.
    // This needs to happen before binding descriptor tables, otherwise D3D12
    // will report validation errors.
    m_rootObject->setResourceStates(m_stateTracking);
    commitBarriers();

    // We kick off binding of shader objects at the root object, and the objects
    // themselves will be responsible for allocating, binding, and filling in
    // any descriptor tables or other root parameters needed.
    //
    bindDescriptorHeaps();
    if (rootObject->bindAsRoot(&context, rootObjectLayout) == SLANG_E_OUT_OF_MEMORY)
    {
        if (!m_transientHeap->canResize())
        {
            return SLANG_E_OUT_OF_MEMORY;
        }

        // If we run out of heap space while binding, allocate new descriptor heaps and try again.
        ID3D12DescriptorHeap* d3dheap = nullptr;
        invalidateDescriptorHeapBinding();
        switch (context.outOfMemoryHeap)
        {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
            SLANG_RETURN_ON_FAIL(m_transientHeap->allocateNewViewDescriptorHeap(m_device));
            d3dheap = m_transientHeap->getCurrentViewHeap().getHeap();
            bindDescriptorHeaps();
            break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            SLANG_RETURN_ON_FAIL(m_transientHeap->allocateNewSamplerDescriptorHeap(m_device));
            d3dheap = m_transientHeap->getCurrentSamplerHeap().getHeap();
            bindDescriptorHeaps();
            break;
        default:
            SLANG_RHI_ASSERT_FAILURE("Shouldn't be here");
            return SLANG_FAIL;
        }

        // Try again.
        SLANG_RETURN_ON_FAIL(rootObject->bindAsRoot(&context, rootObjectLayout));
    }

    return SLANG_OK;
}

void CommandEncoderImpl::bindDescriptorHeaps()
{
    if (!m_descriptorHeapsBound)
    {
        ID3D12DescriptorHeap* heaps[] = {
            m_transientHeap->getCurrentViewHeap().getHeap(),
            m_transientHeap->getCurrentSamplerHeap().getHeap(),
        };
        m_cmdList->SetDescriptorHeaps(SLANG_COUNT_OF(heaps), heaps);
        m_descriptorHeapsBound = true;
    }
}


#if 0

// ResourcePassEncoderImpl


#if 0
void ResourcePassEncoderImpl::clearResourceView(
    IResourceView* view,
    ClearValue* clearValue,
    ClearResourceViewFlags::Enum flags
)
{
    auto viewImpl = checked_cast<ResourceViewImpl*>(view);
    m_commandBuffer->bindDescriptorHeaps();
    switch (view->getViewDesc()->type)
    {
    case IResourceView::Type::RenderTarget:
        m_commandBuffer->m_cmdList
            ->ClearRenderTargetView(viewImpl->m_descriptor.cpuHandle, clearValue->color.floatValues, 0, nullptr);
        break;
    case IResourceView::Type::DepthStencil:
    {
        D3D12_CLEAR_FLAGS clearFlags = (D3D12_CLEAR_FLAGS)0;
        if (flags & ClearResourceViewFlags::ClearDepth)
        {
            clearFlags |= D3D12_CLEAR_FLAG_DEPTH;
        }
        if (flags & ClearResourceViewFlags::ClearStencil)
        {
            clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
        }
        m_commandBuffer->m_cmdList->ClearDepthStencilView(
            viewImpl->m_descriptor.cpuHandle,
            clearFlags,
            clearValue->depthStencil.depth,
            (UINT8)clearValue->depthStencil.stencil,
            0,
            nullptr
        );
        break;
    }
    case IResourceView::Type::UnorderedAccess:
    {
        ID3D12Resource* d3dResource = nullptr;
        D3D12Descriptor descriptor = viewImpl->m_descriptor;
        if (viewImpl->m_isBufferView)
        {
            d3dResource = checked_cast<BufferImpl*>(viewImpl->m_resource.Ptr())->m_resource.getResource();
            // D3D12 requires a UAV descriptor with zero buffer stride for calling ClearUnorderedAccessViewUint/Float.
            viewImpl->getBufferDescriptorForBinding(m_commandBuffer->m_device, viewImpl, 0, descriptor);
        }
        else
        {
            d3dResource = checked_cast<TextureImpl*>(viewImpl->m_resource.Ptr())->m_resource.getResource();
        }
        auto gpuHandleIndex = m_commandBuffer->m_transientHeap->getCurrentViewHeap().allocate(1);
        if (gpuHandleIndex == -1)
        {
            m_commandBuffer->m_transientHeap->allocateNewViewDescriptorHeap(m_commandBuffer->m_device);
            gpuHandleIndex = m_commandBuffer->m_transientHeap->getCurrentViewHeap().allocate(1);
            m_commandBuffer->bindDescriptorHeaps();
        }
        this->m_commandBuffer->m_device->m_device->CopyDescriptorsSimple(
            1,
            m_commandBuffer->m_transientHeap->getCurrentViewHeap().getCpuHandle(gpuHandleIndex),
            descriptor.cpuHandle,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );

        if (flags & ClearResourceViewFlags::FloatClearValues)
        {
            m_commandBuffer->m_cmdList->ClearUnorderedAccessViewFloat(
                m_commandBuffer->m_transientHeap->getCurrentViewHeap().getGpuHandle(gpuHandleIndex),
                descriptor.cpuHandle,
                d3dResource,
                clearValue->color.floatValues,
                0,
                nullptr
            );
        }
        else
        {
            m_commandBuffer->m_cmdList->ClearUnorderedAccessViewUint(
                m_commandBuffer->m_transientHeap->getCurrentViewHeap().getGpuHandle(gpuHandleIndex),
                descriptor.cpuHandle,
                d3dResource,
                clearValue->color.uintValues,
                0,
                nullptr
            );
        }
        break;
    }
    default:
        break;
    }
}
#endif

#endif

} // namespace rhi::d3d12


#if 0


void CommandBufferImpl::reinit()
{
    invalidateDescriptorHeapBinding();
    m_rootShaderObject.init(m_device);
}

#endif
