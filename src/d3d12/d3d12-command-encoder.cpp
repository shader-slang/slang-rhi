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
#include "d3d12-vertex-layout.h"

#include "core/short_vector.h"

namespace rhi::d3d12 {

// CommandEncoderImpl

void CommandEncoderImpl::textureBarrier(GfxCount count, ITexture* const* textures, ResourceState src, ResourceState dst)
{
    short_vector<D3D12_RESOURCE_BARRIER> barriers;

    for (GfxIndex i = 0; i < count; i++)
    {
        auto textureImpl = static_cast<TextureImpl*>(textures[i]);
        auto d3dFormat = D3DUtil::getMapFormat(textureImpl->getDesc()->format);
        auto textureDesc = textureImpl->getDesc();
        D3D12_RESOURCE_BARRIER barrier;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        if (src == dst && src == ResourceState::UnorderedAccess)
        {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = textureImpl->m_resource.getResource();
        }
        else
        {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.StateBefore = D3DUtil::getResourceState(src);
            barrier.Transition.StateAfter = D3DUtil::getResourceState(dst);
            if (barrier.Transition.StateBefore == barrier.Transition.StateAfter)
                continue;
            barrier.Transition.pResource = textureImpl->m_resource.getResource();
            auto planeCount = D3DUtil::getPlaneSliceCount(D3DUtil::getMapFormat(textureImpl->getDesc()->format));
            auto arraySize = textureDesc->arraySize;
            if (arraySize == 0)
                arraySize = 1;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }
        barriers.push_back(barrier);
    }
    if (!barriers.empty())
    {
        m_commandBuffer->m_cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
    }
}

void CommandEncoderImpl::textureSubresourceBarrier(
    ITexture* texture,
    SubresourceRange subresourceRange,
    ResourceState src,
    ResourceState dst
)
{
    auto textureImpl = static_cast<TextureImpl*>(texture);

    short_vector<D3D12_RESOURCE_BARRIER> barriers;
    D3D12_RESOURCE_BARRIER barrier;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    if (src == dst && src == ResourceState::UnorderedAccess)
    {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = textureImpl->m_resource.getResource();
        barriers.push_back(barrier);
    }
    else
    {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.StateBefore = D3DUtil::getResourceState(src);
        barrier.Transition.StateAfter = D3DUtil::getResourceState(dst);
        if (barrier.Transition.StateBefore == barrier.Transition.StateAfter)
            return;
        barrier.Transition.pResource = textureImpl->m_resource.getResource();
        auto d3dFormat = D3DUtil::getMapFormat(textureImpl->getDesc()->format);
        auto aspectMask = (int32_t)subresourceRange.aspectMask;
        if (subresourceRange.aspectMask == TextureAspect::Default)
            aspectMask = (int32_t)TextureAspect::Color;
        while (aspectMask)
        {
            auto aspect = math::getLowestBit((int32_t)aspectMask);
            aspectMask &= ~aspect;
            auto planeIndex = D3DUtil::getPlaneSlice(d3dFormat, (TextureAspect)aspect);
            for (GfxCount layer = 0; layer < subresourceRange.layerCount; layer++)
            {
                for (GfxCount mip = 0; mip < subresourceRange.mipLevelCount; mip++)
                {
                    barrier.Transition.Subresource = D3DUtil::getSubresourceIndex(
                        mip + subresourceRange.mipLevel,
                        layer + subresourceRange.baseArrayLayer,
                        planeIndex,
                        textureImpl->getDesc()->numMipLevels,
                        textureImpl->getDesc()->arraySize
                    );
                    barriers.push_back(barrier);
                }
            }
        }
    }
    m_commandBuffer->m_cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
}

void CommandEncoderImpl::bufferBarrier(GfxCount count, IBuffer* const* buffers, ResourceState src, ResourceState dst)
{
    short_vector<D3D12_RESOURCE_BARRIER, 16> barriers;
    for (GfxIndex i = 0; i < count; i++)
    {
        auto bufferImpl = static_cast<BufferImpl*>(buffers[i]);

        D3D12_RESOURCE_BARRIER barrier = {};
        // If the src == dst, it must be a UAV barrier.
        barrier.Type = (src == dst && dst == ResourceState::UnorderedAccess) ? D3D12_RESOURCE_BARRIER_TYPE_UAV
                                                                             : D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

        if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV)
        {
            barrier.UAV.pResource = bufferImpl->m_resource;
        }
        else
        {
            barrier.Transition.pResource = bufferImpl->m_resource;
            barrier.Transition.StateBefore = D3DUtil::getResourceState(src);
            barrier.Transition.StateAfter = D3DUtil::getResourceState(dst);
            barrier.Transition.Subresource = 0;
            if (barrier.Transition.StateAfter == barrier.Transition.StateBefore)
                continue;
        }
        barriers.push_back(barrier);
    }
    if (!barriers.empty())
    {
        m_commandBuffer->m_cmdList4->ResourceBarrier((UINT)barriers.size(), barriers.data());
    }
}

void CommandEncoderImpl::beginDebugEvent(const char* name, float rgbColor[3])
{
    auto beginEvent = m_commandBuffer->m_renderer->m_BeginEventOnCommandList;
    if (beginEvent)
    {
        beginEvent(
            m_commandBuffer->m_cmdList,
            0xff000000 | (uint8_t(rgbColor[0] * 255.0f) << 16) | (uint8_t(rgbColor[1] * 255.0f) << 8) |
                uint8_t(rgbColor[2] * 255.0f),
            name
        );
    }
}

void CommandEncoderImpl::endDebugEvent()
{
    auto endEvent = m_commandBuffer->m_renderer->m_EndEventOnCommandList;
    if (endEvent)
    {
        endEvent(m_commandBuffer->m_cmdList);
    }
}

void CommandEncoderImpl::writeTimestamp(IQueryPool* pool, GfxIndex index)
{
    static_cast<QueryPoolImpl*>(pool)->writeTimestamp(m_commandBuffer->m_cmdList, index);
}


int CommandEncoderImpl::getBindPointIndex(PipelineType type)
{
    switch (type)
    {
    case PipelineType::Graphics:
        return 0;
    case PipelineType::Compute:
        return 1;
    case PipelineType::RayTracing:
        return 2;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unknown pipeline type.");
        return -1;
    }
}

void CommandEncoderImpl::init(CommandBufferImpl* commandBuffer)
{
    m_commandBuffer = commandBuffer;
    m_d3dCmdList = m_commandBuffer->m_cmdList;
    m_d3dCmdList6 = m_commandBuffer->m_cmdList6;
    m_renderer = commandBuffer->m_renderer;
    m_transientHeap = commandBuffer->m_transientHeap;
    m_device = commandBuffer->m_renderer->m_device;
}

Result CommandEncoderImpl::bindPipelineImpl(IPipeline* pipeline, IShaderObject** outRootObject)
{
    m_currentPipeline = static_cast<PipelineBase*>(pipeline);
    auto rootObject = &m_commandBuffer->m_rootShaderObject;
    m_commandBuffer->m_mutableRootShaderObject = nullptr;
    SLANG_RETURN_ON_FAIL(rootObject->reset(
        m_renderer,
        m_currentPipeline->getProgram<ShaderProgramImpl>()->m_rootObjectLayout,
        m_commandBuffer->m_transientHeap
    ));
    *outRootObject = rootObject;
    m_bindingDirty = true;
    return SLANG_OK;
}

Result CommandEncoderImpl::bindPipelineWithRootObjectImpl(IPipeline* pipeline, IShaderObject* rootObject)
{
    m_currentPipeline = static_cast<PipelineBase*>(pipeline);
    m_commandBuffer->m_mutableRootShaderObject = static_cast<MutableRootShaderObjectImpl*>(rootObject);
    m_bindingDirty = true;
    return SLANG_OK;
}

Result CommandEncoderImpl::_bindRenderState(Submitter* submitter, RefPtr<PipelineBase>& newPipeline)
{
    RootShaderObjectImpl* rootObjectImpl = m_commandBuffer->m_mutableRootShaderObject
                                               ? m_commandBuffer->m_mutableRootShaderObject.Ptr()
                                               : &m_commandBuffer->m_rootShaderObject;
    SLANG_RETURN_ON_FAIL(m_renderer->maybeSpecializePipeline(m_currentPipeline, rootObjectImpl, newPipeline));
    PipelineBase* newPipelineImpl = static_cast<PipelineBase*>(newPipeline.Ptr());
    auto commandList = m_d3dCmdList;
    auto pipelineTypeIndex = (int)newPipelineImpl->desc.type;
    auto programImpl = static_cast<ShaderProgramImpl*>(newPipelineImpl->m_program.Ptr());
    SLANG_RETURN_ON_FAIL(newPipelineImpl->ensureAPIPipelineCreated());
    submitter->setRootSignature(programImpl->m_rootObjectLayout->m_rootSignature);
    submitter->setPipeline(newPipelineImpl);
    RootShaderObjectLayoutImpl* rootLayoutImpl = programImpl->m_rootObjectLayout;

    // We need to set up a context for binding shader objects to the pipeline state.
    // This type mostly exists to bundle together a bunch of parameters that would
    // otherwise need to be tunneled down through all the shader object binding
    // logic.
    //
    BindingContext context = {};
    context.encoder = this;
    context.submitter = submitter;
    context.device = m_renderer;
    context.transientHeap = m_transientHeap;
    context.outOfMemoryHeap = (D3D12_DESCRIPTOR_HEAP_TYPE)(-1);
    // We kick off binding of shader objects at the root object, and the objects
    // themselves will be responsible for allocating, binding, and filling in
    // any descriptor tables or other root parameters needed.
    //
    m_commandBuffer->bindDescriptorHeaps();
    if (rootObjectImpl->bindAsRoot(&context, rootLayoutImpl) == SLANG_E_OUT_OF_MEMORY)
    {
        if (!m_transientHeap->canResize())
        {
            return SLANG_E_OUT_OF_MEMORY;
        }

        // If we run out of heap space while binding, allocate new descriptor heaps and try again.
        ID3D12DescriptorHeap* d3dheap = nullptr;
        m_commandBuffer->invalidateDescriptorHeapBinding();
        switch (context.outOfMemoryHeap)
        {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
            SLANG_RETURN_ON_FAIL(m_transientHeap->allocateNewViewDescriptorHeap(m_renderer));
            d3dheap = m_transientHeap->getCurrentViewHeap().getHeap();
            m_commandBuffer->bindDescriptorHeaps();
            break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            SLANG_RETURN_ON_FAIL(m_transientHeap->allocateNewSamplerDescriptorHeap(m_renderer));
            d3dheap = m_transientHeap->getCurrentSamplerHeap().getHeap();
            m_commandBuffer->bindDescriptorHeaps();
            break;
        default:
            SLANG_RHI_ASSERT_FAILURE("Shouldn't be here");
            return SLANG_FAIL;
        }

        // Try again.
        SLANG_RETURN_ON_FAIL(rootObjectImpl->bindAsRoot(&context, rootLayoutImpl));
    }

    return SLANG_OK;
}

// ResourceCommandEncoderImpl

void ResourceCommandEncoderImpl::copyTexture(
    ITexture* dst,
    ResourceState dstState,
    SubresourceRange dstSubresource,
    Offset3D dstOffset,
    ITexture* src,
    ResourceState srcState,
    SubresourceRange srcSubresource,
    Offset3D srcOffset,
    Extents extent
)
{
    auto dstTexture = static_cast<TextureImpl*>(dst);
    auto srcTexture = static_cast<TextureImpl*>(src);

    if (dstSubresource.layerCount == 0 && dstSubresource.mipLevelCount == 0 && srcSubresource.layerCount == 0 &&
        srcSubresource.mipLevelCount == 0)
    {
        m_commandBuffer->m_cmdList->CopyResource(
            dstTexture->m_resource.getResource(),
            srcTexture->m_resource.getResource()
        );
        return;
    }

    auto d3dFormat = D3DUtil::getMapFormat(dstTexture->getDesc()->format);
    auto aspectMask = (int32_t)dstSubresource.aspectMask;
    if (dstSubresource.aspectMask == TextureAspect::Default)
        aspectMask = (int32_t)TextureAspect::Color;
    while (aspectMask)
    {
        auto aspect = math::getLowestBit((int32_t)aspectMask);
        aspectMask &= ~aspect;
        auto planeIndex = D3DUtil::getPlaneSlice(d3dFormat, (TextureAspect)aspect);
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
                    dstTexture->getDesc()->numMipLevels,
                    dstTexture->getDesc()->arraySize
                );

                D3D12_TEXTURE_COPY_LOCATION srcRegion = {};
                srcRegion.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                srcRegion.pResource = srcTexture->m_resource.getResource();
                srcRegion.SubresourceIndex = D3DUtil::getSubresourceIndex(
                    srcSubresource.mipLevel + mipLevel,
                    srcSubresource.baseArrayLayer + layer,
                    planeIndex,
                    srcTexture->getDesc()->numMipLevels,
                    srcTexture->getDesc()->arraySize
                );

                D3D12_BOX srcBox = {};
                srcBox.left = srcOffset.x;
                srcBox.top = srcOffset.y;
                srcBox.front = srcOffset.z;
                srcBox.right = srcBox.left + extent.width;
                srcBox.bottom = srcBox.top + extent.height;
                srcBox.back = srcBox.front + extent.depth;

                m_commandBuffer->m_cmdList
                    ->CopyTextureRegion(&dstRegion, dstOffset.x, dstOffset.y, dstOffset.z, &srcRegion, &srcBox);
            }
        }
    }
}

void ResourceCommandEncoderImpl::uploadTextureData(
    ITexture* dst,
    SubresourceRange subResourceRange,
    Offset3D offset,
    Extents extent,
    SubresourceData* subResourceData,
    GfxCount subResourceDataCount
)
{
    auto dstTexture = static_cast<TextureImpl*>(dst);
    auto baseSubresourceIndex = D3DUtil::getSubresourceIndex(
        subResourceRange.mipLevel,
        subResourceRange.baseArrayLayer,
        0,
        dstTexture->getDesc()->numMipLevels,
        dstTexture->getDesc()->arraySize
    );
    auto textureSize = dstTexture->getDesc()->size;
    FormatInfo formatInfo = {};
    rhiGetFormatInfo(dstTexture->getDesc()->format, &formatInfo);
    for (GfxCount i = 0; i < subResourceDataCount; i++)
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
        uint32_t mipLevel = D3DUtil::getSubresourceMipLevel(subresourceIndex, dstTexture->getDesc()->numMipLevels);
        if (extent.width != kRemainingTextureSize)
        {
            footprint.Footprint.Width = extent.width;
        }
        else
        {
            footprint.Footprint.Width = std::max(1, (textureSize.width >> mipLevel)) - offset.x;
        }
        if (extent.height != kRemainingTextureSize)
        {
            footprint.Footprint.Height = extent.height;
        }
        else
        {
            footprint.Footprint.Height = std::max(1, (textureSize.height >> mipLevel)) - offset.y;
        }
        if (extent.depth != kRemainingTextureSize)
        {
            footprint.Footprint.Depth = extent.depth;
        }
        else
        {
            footprint.Footprint.Depth = std::max(1, (textureSize.depth >> mipLevel)) - offset.z;
        }
        auto rowSize = (footprint.Footprint.Width + formatInfo.blockWidth - 1) / formatInfo.blockWidth *
                       formatInfo.blockSizeInBytes;
        auto rowCount = (footprint.Footprint.Height + formatInfo.blockHeight - 1) / formatInfo.blockHeight;
        footprint.Footprint.RowPitch =
            (UINT)D3DUtil::calcAligned(rowSize, (uint32_t)D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

        auto bufferSize = footprint.Footprint.RowPitch * rowCount * footprint.Footprint.Depth;

        IBuffer* stagingBuffer;
        Offset stagingBufferOffset = 0;
        m_commandBuffer->m_transientHeap
            ->allocateStagingBuffer(bufferSize, stagingBuffer, stagingBufferOffset, MemoryType::Upload, true);
        SLANG_RHI_ASSERT(stagingBufferOffset == 0);
        BufferImpl* bufferImpl = static_cast<BufferImpl*>(stagingBuffer);
        uint8_t* bufferData = nullptr;
        D3D12_RANGE mapRange = {0, 0};
        bufferImpl->m_resource.getResource()->Map(0, &mapRange, (void**)&bufferData);
        for (uint32_t z = 0; z < footprint.Footprint.Depth; z++)
        {
            auto imageStart = bufferData + footprint.Footprint.RowPitch * rowCount * (Size)z;
            auto srcData = (uint8_t*)subResourceData->data + subResourceData->strideZ * z;
            for (uint32_t row = 0; row < rowCount; row++)
            {
                memcpy(
                    imageStart + row * (Size)footprint.Footprint.RowPitch,
                    srcData + subResourceData->strideY * row,
                    rowSize
                );
            }
        }
        bufferImpl->m_resource.getResource()->Unmap(0, nullptr);
        srcRegion.pResource = bufferImpl->m_resource.getResource();
        m_commandBuffer->m_cmdList->CopyTextureRegion(&dstRegion, offset.x, offset.y, offset.z, &srcRegion, nullptr);
    }
}

void ResourceCommandEncoderImpl::clearResourceView(
    IResourceView* view,
    ClearValue* clearValue,
    ClearResourceViewFlags::Enum flags
)
{
    auto viewImpl = static_cast<ResourceViewImpl*>(view);
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
            d3dResource = static_cast<BufferImpl*>(viewImpl->m_resource.Ptr())->m_resource.getResource();
            // D3D12 requires a UAV descriptor with zero buffer stride for calling ClearUnorderedAccessViewUint/Float.
            viewImpl->getBufferDescriptorForBinding(m_commandBuffer->m_renderer, viewImpl, 0, descriptor);
        }
        else
        {
            d3dResource = static_cast<TextureImpl*>(viewImpl->m_resource.Ptr())->m_resource.getResource();
        }
        auto gpuHandleIndex = m_commandBuffer->m_transientHeap->getCurrentViewHeap().allocate(1);
        if (gpuHandleIndex == -1)
        {
            m_commandBuffer->m_transientHeap->allocateNewViewDescriptorHeap(m_commandBuffer->m_renderer);
            gpuHandleIndex = m_commandBuffer->m_transientHeap->getCurrentViewHeap().allocate(1);
            m_commandBuffer->bindDescriptorHeaps();
        }
        this->m_commandBuffer->m_renderer->m_device->CopyDescriptorsSimple(
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

void ResourceCommandEncoderImpl::resolveResource(
    ITexture* source,
    ResourceState sourceState,
    SubresourceRange sourceRange,
    ITexture* dest,
    ResourceState destState,
    SubresourceRange destRange
)
{
    auto srcTexture = static_cast<TextureImpl*>(source);
    auto srcDesc = srcTexture->getDesc();
    auto dstTexture = static_cast<TextureImpl*>(dest);
    auto dstDesc = dstTexture->getDesc();

    for (GfxIndex layer = 0; layer < sourceRange.layerCount; ++layer)
    {
        for (GfxIndex mip = 0; mip < sourceRange.mipLevelCount; ++mip)
        {
            auto srcSubresourceIndex = D3DUtil::getSubresourceIndex(
                mip + sourceRange.mipLevel,
                layer + sourceRange.baseArrayLayer,
                0,
                srcDesc->numMipLevels,
                srcDesc->arraySize
            );
            auto dstSubresourceIndex = D3DUtil::getSubresourceIndex(
                mip + destRange.mipLevel,
                layer + destRange.baseArrayLayer,
                0,
                dstDesc->numMipLevels,
                dstDesc->arraySize
            );

            DXGI_FORMAT format = D3DUtil::getMapFormat(srcDesc->format);

            m_commandBuffer->m_cmdList->ResolveSubresource(
                dstTexture->m_resource.getResource(),
                dstSubresourceIndex,
                srcTexture->m_resource.getResource(),
                srcSubresourceIndex,
                format
            );
        }
    }
}

void ResourceCommandEncoderImpl::resolveQuery(
    IQueryPool* queryPool,
    GfxIndex index,
    GfxCount count,
    IBuffer* buffer,
    Offset offset
)
{
    auto queryBase = static_cast<QueryPoolBase*>(queryPool);
    switch (queryBase->m_desc.type)
    {
    case QueryType::AccelerationStructureCompactedSize:
    case QueryType::AccelerationStructureCurrentSize:
    case QueryType::AccelerationStructureSerializedSize:
    {
        auto queryPoolImpl = static_cast<PlainBufferProxyQueryPoolImpl*>(queryPool);
        auto bufferImpl = static_cast<BufferImpl*>(buffer);
        auto srcQueryBuffer = queryPoolImpl->m_buffer->m_resource.getResource();

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.pResource = srcQueryBuffer;
        m_commandBuffer->m_cmdList->ResourceBarrier(1, &barrier);

        m_commandBuffer->m_cmdList->CopyBufferRegion(
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
        m_commandBuffer->m_cmdList->ResourceBarrier(1, &barrier);
    }
    break;
    default:
    {
        auto queryPoolImpl = static_cast<QueryPoolImpl*>(queryPool);
        auto bufferImpl = static_cast<BufferImpl*>(buffer);
        m_commandBuffer->m_cmdList->ResolveQueryData(
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

void ResourceCommandEncoderImpl::copyTextureToBuffer(
    IBuffer* dst,
    Offset dstOffset,
    Size dstSize,
    Size dstRowStride,
    ITexture* src,
    ResourceState srcState,
    SubresourceRange srcSubresource,
    Offset3D srcOffset,
    Extents extent
)
{
    SLANG_RHI_ASSERT(srcSubresource.mipLevelCount <= 1);

    auto srcTexture = static_cast<TextureImpl*>(src);
    auto dstBuffer = static_cast<BufferImpl*>(dst);
    auto baseSubresourceIndex = D3DUtil::getSubresourceIndex(
        srcSubresource.mipLevel,
        srcSubresource.baseArrayLayer,
        0,
        srcTexture->getDesc()->numMipLevels,
        srcTexture->getDesc()->arraySize
    );
    auto textureSize = srcTexture->getDesc()->size;
    FormatInfo formatInfo = {};
    rhiGetFormatInfo(srcTexture->getDesc()->format, &formatInfo);
    if (srcSubresource.mipLevelCount == 0)
        srcSubresource.mipLevelCount = srcTexture->getDesc()->numMipLevels;
    if (srcSubresource.layerCount == 0)
        srcSubresource.layerCount = srcTexture->getDesc()->arraySize;

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
            srcTexture->getDesc()->numMipLevels,
            srcTexture->getDesc()->arraySize
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
            footprint.Footprint.Width = std::max(1, (textureSize.width >> mipLevel)) - srcOffset.x;
        }
        if (extent.height != 0xFFFFFFFF)
        {
            footprint.Footprint.Height = extent.height;
        }
        else
        {
            footprint.Footprint.Height = std::max(1, (textureSize.height >> mipLevel)) - srcOffset.y;
        }
        if (extent.depth != 0xFFFFFFFF)
        {
            footprint.Footprint.Depth = extent.depth;
        }
        else
        {
            footprint.Footprint.Depth = std::max(1, (textureSize.depth >> mipLevel)) - srcOffset.z;
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
        m_commandBuffer->m_cmdList->CopyTextureRegion(&dstRegion, 0, 0, 0, &srcRegion, &srcBox);
    }
}

void ResourceCommandEncoderImpl::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    auto dstBuffer = static_cast<BufferImpl*>(dst);
    auto srcBuffer = static_cast<BufferImpl*>(src);

    m_commandBuffer->m_cmdList->CopyBufferRegion(
        dstBuffer->m_resource.getResource(),
        dstOffset,
        srcBuffer->m_resource.getResource(),
        srcOffset,
        size
    );
}

void ResourceCommandEncoderImpl::uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data)
{
    uploadBufferDataImpl(
        m_commandBuffer->m_renderer->m_device,
        m_commandBuffer->m_cmdList,
        m_commandBuffer->m_transientHeap,
        static_cast<BufferImpl*>(dst),
        offset,
        size,
        data
    );
}

// RenderCommandEncoderImpl

void RenderCommandEncoderImpl::init(
    DeviceImpl* renderer,
    TransientResourceHeapImpl* transientHeap,
    CommandBufferImpl* cmdBuffer,
    RenderPassLayoutImpl* renderPass,
    FramebufferImpl* framebuffer
)
{
    CommandEncoderImpl::init(cmdBuffer);
    m_preCmdList = nullptr;
    m_renderPass = renderPass;
    m_framebuffer = framebuffer;
    m_transientHeap = transientHeap;
    m_boundVertexBuffers.clear();
    m_boundIndexBuffer = nullptr;
    m_primitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    m_primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    m_boundIndexFormat = DXGI_FORMAT_UNKNOWN;
    m_boundIndexOffset = 0;
    m_currentPipeline = nullptr;

    // Set render target states.
    if (!framebuffer)
    {
        return;
    }
    m_d3dCmdList->OMSetRenderTargets(
        (UINT)framebuffer->renderTargetViews.size(),
        framebuffer->renderTargetDescriptors.data(),
        FALSE,
        framebuffer->depthStencilView ? &framebuffer->depthStencilDescriptor : nullptr
    );

    // Issue clear commands based on render pass set up.
    for (Index i = 0; i < framebuffer->renderTargetViews.size(); i++)
    {
        if (i >= renderPass->m_renderTargetAccesses.size())
            continue;

        auto& access = renderPass->m_renderTargetAccesses[i];

        // Transit resource states.
        {
            D3D12BarrierSubmitter submitter(m_d3dCmdList);
            auto resourceViewImpl = framebuffer->renderTargetViews[i].Ptr();
            if (resourceViewImpl)
            {
                auto texture = static_cast<TextureImpl*>(resourceViewImpl->m_resource.Ptr());
                if (texture)
                {
                    D3D12_RESOURCE_STATES initialState;
                    if (access.initialState == ResourceState::Undefined)
                    {
                        initialState = texture->m_defaultState;
                    }
                    else
                    {
                        initialState = D3DUtil::getResourceState(access.initialState);
                    }
                    texture->m_resource.transition(initialState, D3D12_RESOURCE_STATE_RENDER_TARGET, submitter);
                }
            }
        }
        // Clear.
        if (access.loadOp == IRenderPassLayout::TargetLoadOp::Clear)
        {
            m_d3dCmdList->ClearRenderTargetView(
                framebuffer->renderTargetDescriptors[i],
                framebuffer->renderTargetClearValues[i].values,
                0,
                nullptr
            );
        }
    }

    if (renderPass->m_hasDepthStencil)
    {
        // Transit resource states.
        {
            D3D12BarrierSubmitter submitter(m_d3dCmdList);
            auto resourceViewImpl = framebuffer->depthStencilView.Ptr();
            auto texture = static_cast<TextureImpl*>(resourceViewImpl->m_resource.Ptr());
            D3D12_RESOURCE_STATES initialState;
            if (renderPass->m_depthStencilAccess.initialState == ResourceState::Undefined)
            {
                initialState = texture->m_defaultState;
            }
            else
            {
                initialState = D3DUtil::getResourceState(renderPass->m_depthStencilAccess.initialState);
            }
            texture->m_resource.transition(initialState, D3D12_RESOURCE_STATE_DEPTH_WRITE, submitter);
        }
        // Clear.
        uint32_t clearFlags = 0;
        if (renderPass->m_depthStencilAccess.loadOp == IRenderPassLayout::TargetLoadOp::Clear)
        {
            clearFlags |= D3D12_CLEAR_FLAG_DEPTH;
        }
        if (renderPass->m_depthStencilAccess.stencilLoadOp == IRenderPassLayout::TargetLoadOp::Clear)
        {
            clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
        }
        if (clearFlags)
        {
            m_d3dCmdList->ClearDepthStencilView(
                framebuffer->depthStencilDescriptor,
                (D3D12_CLEAR_FLAGS)clearFlags,
                framebuffer->depthStencilClearValue.depth,
                framebuffer->depthStencilClearValue.stencil,
                0,
                nullptr
            );
        }
    }
}

Result RenderCommandEncoderImpl::bindPipeline(IPipeline* state, IShaderObject** outRootObject)
{
    return bindPipelineImpl(state, outRootObject);
}

Result RenderCommandEncoderImpl::bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject)
{
    return bindPipelineWithRootObjectImpl(state, rootObject);
}

void RenderCommandEncoderImpl::setViewports(GfxCount count, const Viewport* viewports)
{
    static const int kMaxViewports = D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    SLANG_RHI_ASSERT(count <= kMaxViewports && count <= kMaxRTVCount);
    for (GfxIndex ii = 0; ii < count; ++ii)
    {
        auto& inViewport = viewports[ii];
        auto& dxViewport = m_viewports[ii];

        dxViewport.TopLeftX = inViewport.originX;
        dxViewport.TopLeftY = inViewport.originY;
        dxViewport.Width = inViewport.extentX;
        dxViewport.Height = inViewport.extentY;
        dxViewport.MinDepth = inViewport.minZ;
        dxViewport.MaxDepth = inViewport.maxZ;
    }
    m_d3dCmdList->RSSetViewports(UINT(count), m_viewports);
}

void RenderCommandEncoderImpl::setScissorRects(GfxCount count, const ScissorRect* rects)
{
    static const int kMaxScissorRects = D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    SLANG_RHI_ASSERT(count <= kMaxScissorRects && count <= kMaxRTVCount);

    for (GfxIndex ii = 0; ii < count; ++ii)
    {
        auto& inRect = rects[ii];
        auto& dxRect = m_scissorRects[ii];

        dxRect.left = LONG(inRect.minX);
        dxRect.top = LONG(inRect.minY);
        dxRect.right = LONG(inRect.maxX);
        dxRect.bottom = LONG(inRect.maxY);
    }

    m_d3dCmdList->RSSetScissorRects(UINT(count), m_scissorRects);
}

void RenderCommandEncoderImpl::setPrimitiveTopology(PrimitiveTopology topology)
{
    m_primitiveTopologyType = D3DUtil::getPrimitiveType(topology);
    m_primitiveTopology = D3DUtil::getPrimitiveTopology(topology);
}

void RenderCommandEncoderImpl::setVertexBuffers(
    GfxIndex startSlot,
    GfxCount slotCount,
    IBuffer* const* buffers,
    const Offset* offsets
)
{
    {
        const Index num = startSlot + slotCount;
        if (num > m_boundVertexBuffers.size())
        {
            m_boundVertexBuffers.resize(num);
        }
    }

    for (GfxIndex i = 0; i < slotCount; i++)
    {
        BufferImpl* buffer = static_cast<BufferImpl*>(buffers[i]);

        BoundVertexBuffer& boundBuffer = m_boundVertexBuffers[startSlot + i];
        boundBuffer.m_buffer = buffer;
        boundBuffer.m_offset = int(offsets[i]);
    }
}

void RenderCommandEncoderImpl::setIndexBuffer(IBuffer* buffer, Format indexFormat, Offset offset)
{
    m_boundIndexBuffer = (BufferImpl*)buffer;
    m_boundIndexFormat = D3DUtil::getMapFormat(indexFormat);
    m_boundIndexOffset = (UINT)offset;
}

Result RenderCommandEncoderImpl::prepareDraw()
{
    auto pipeline = m_currentPipeline.Ptr();
    if (!pipeline || (pipeline->desc.type != PipelineType::Graphics))
    {
        return SLANG_FAIL;
    }

    // Submit - setting for graphics
    {
        GraphicsSubmitter submitter(m_d3dCmdList);
        RefPtr<PipelineBase> newPipeline;
        SLANG_RETURN_ON_FAIL(_bindRenderState(&submitter, newPipeline));
    }

    m_d3dCmdList->IASetPrimitiveTopology(m_primitiveTopology);

    // Set up vertex buffer views
    {
        auto inputLayout = (InputLayoutImpl*)pipeline->inputLayout.Ptr();
        if (inputLayout)
        {
            int numVertexViews = 0;
            D3D12_VERTEX_BUFFER_VIEW vertexViews[16];
            for (Index i = 0; i < m_boundVertexBuffers.size(); i++)
            {
                const BoundVertexBuffer& boundVertexBuffer = m_boundVertexBuffers[i];
                BufferImpl* buffer = boundVertexBuffer.m_buffer;
                if (buffer)
                {
                    D3D12_VERTEX_BUFFER_VIEW& vertexView = vertexViews[numVertexViews++];
                    vertexView.BufferLocation =
                        buffer->m_resource.getResource()->GetGPUVirtualAddress() + boundVertexBuffer.m_offset;
                    vertexView.SizeInBytes = UINT(buffer->getDesc()->size - boundVertexBuffer.m_offset);
                    vertexView.StrideInBytes = inputLayout->m_vertexStreamStrides[i];
                }
            }
            m_d3dCmdList->IASetVertexBuffers(0, numVertexViews, vertexViews);
        }
    }
    // Set up index buffer
    if (m_boundIndexBuffer)
    {
        D3D12_INDEX_BUFFER_VIEW indexBufferView;
        indexBufferView.BufferLocation =
            m_boundIndexBuffer->m_resource.getResource()->GetGPUVirtualAddress() + m_boundIndexOffset;
        indexBufferView.SizeInBytes = UINT(m_boundIndexBuffer->getDesc()->size - m_boundIndexOffset);
        indexBufferView.Format = m_boundIndexFormat;

        m_d3dCmdList->IASetIndexBuffer(&indexBufferView);
    }
    return SLANG_OK;
}

Result RenderCommandEncoderImpl::draw(GfxCount vertexCount, GfxIndex startVertex)
{
    SLANG_RETURN_ON_FAIL(prepareDraw());
    m_d3dCmdList->DrawInstanced((uint32_t)vertexCount, 1, (uint32_t)startVertex, 0);
    return SLANG_OK;
}

Result RenderCommandEncoderImpl::drawIndexed(GfxCount indexCount, GfxIndex startIndex, GfxIndex baseVertex)
{
    SLANG_RETURN_ON_FAIL(prepareDraw());
    m_d3dCmdList->DrawIndexedInstanced((uint32_t)indexCount, 1, (uint32_t)startIndex, (uint32_t)baseVertex, 0);
    return SLANG_OK;
}

void RenderCommandEncoderImpl::endEncoding()
{
    CommandEncoderImpl::endEncodingImpl();
    if (!m_framebuffer)
        return;
    // Issue clear commands based on render pass set up.
    for (Index i = 0; i < m_renderPass->m_renderTargetAccesses.size(); i++)
    {
        auto& access = m_renderPass->m_renderTargetAccesses[i];

        // Transit resource states.
        {
            D3D12BarrierSubmitter submitter(m_d3dCmdList);
            auto resourceViewImpl = m_framebuffer->renderTargetViews[i].Ptr();
            if (!resourceViewImpl)
                continue;
            auto texture = static_cast<TextureImpl*>(resourceViewImpl->m_resource.Ptr());
            if (texture)
            {
                texture->m_resource.transition(
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3DUtil::getResourceState(access.finalState),
                    submitter
                );
            }
        }
    }

    if (m_renderPass->m_hasDepthStencil)
    {
        // Transit resource states.
        D3D12BarrierSubmitter submitter(m_d3dCmdList);
        auto resourceViewImpl = m_framebuffer->depthStencilView.Ptr();
        auto texture = static_cast<TextureImpl*>(resourceViewImpl->m_resource.Ptr());
        texture->m_resource.transition(
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3DUtil::getResourceState(m_renderPass->m_depthStencilAccess.finalState),
            submitter
        );
    }
    m_framebuffer = nullptr;
}

void RenderCommandEncoderImpl::setStencilReference(uint32_t referenceValue)
{
    m_d3dCmdList->OMSetStencilRef((UINT)referenceValue);
}

Result RenderCommandEncoderImpl::drawIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    SLANG_RETURN_ON_FAIL(prepareDraw());

    auto argBufferImpl = static_cast<BufferImpl*>(argBuffer);
    auto countBufferImpl = static_cast<BufferImpl*>(countBuffer);

    m_d3dCmdList->ExecuteIndirect(
        m_renderer->drawIndirectCmdSignature,
        (uint32_t)maxDrawCount,
        argBufferImpl->m_resource,
        (uint64_t)argOffset,
        countBufferImpl ? countBufferImpl->m_resource.getResource() : nullptr,
        (uint64_t)countOffset
    );
    return SLANG_OK;
}

Result RenderCommandEncoderImpl::drawIndexedIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    SLANG_RETURN_ON_FAIL(prepareDraw());

    auto argBufferImpl = static_cast<BufferImpl*>(argBuffer);
    auto countBufferImpl = static_cast<BufferImpl*>(countBuffer);

    m_d3dCmdList->ExecuteIndirect(
        m_renderer->drawIndexedIndirectCmdSignature,
        (uint32_t)maxDrawCount,
        argBufferImpl->m_resource,
        (uint64_t)argOffset,
        countBufferImpl ? countBufferImpl->m_resource.getResource() : nullptr,
        (uint64_t)countOffset
    );

    return SLANG_OK;
}

Result RenderCommandEncoderImpl::setSamplePositions(
    GfxCount samplesPerPixel,
    GfxCount pixelCount,
    const SamplePosition* samplePositions
)
{
    if (m_commandBuffer->m_cmdList1)
    {
        m_commandBuffer->m_cmdList1->SetSamplePositions(
            (uint32_t)samplesPerPixel,
            (uint32_t)pixelCount,
            (D3D12_SAMPLE_POSITION*)samplePositions
        );
        return SLANG_OK;
    }
    return SLANG_E_NOT_AVAILABLE;
}

Result RenderCommandEncoderImpl::drawInstanced(
    GfxCount vertexCount,
    GfxCount instanceCount,
    GfxIndex startVertex,
    GfxIndex startInstanceLocation
)
{
    SLANG_RETURN_ON_FAIL(prepareDraw());
    m_d3dCmdList->DrawInstanced(
        (uint32_t)vertexCount,
        (uint32_t)instanceCount,
        (uint32_t)startVertex,
        (uint32_t)startInstanceLocation
    );
    return SLANG_OK;
}

Result RenderCommandEncoderImpl::drawIndexedInstanced(
    GfxCount indexCount,
    GfxCount instanceCount,
    GfxIndex startIndexLocation,
    GfxIndex baseVertexLocation,
    GfxIndex startInstanceLocation
)
{
    SLANG_RETURN_ON_FAIL(prepareDraw());
    m_d3dCmdList->DrawIndexedInstanced(
        (uint32_t)indexCount,
        (uint32_t)instanceCount,
        (uint32_t)startIndexLocation,
        baseVertexLocation,
        (uint32_t)startInstanceLocation
    );
    return SLANG_OK;
}

Result RenderCommandEncoderImpl::drawMeshTasks(int x, int y, int z)
{
    SLANG_RETURN_ON_FAIL(prepareDraw());
    m_d3dCmdList6->DispatchMesh(x, y, z);
    return SLANG_OK;
}

// ComputeCommandEncoderImpl

void ComputeCommandEncoderImpl::endEncoding()
{
    CommandEncoderImpl::endEncodingImpl();
}

void ComputeCommandEncoderImpl::init(
    DeviceImpl* renderer,
    TransientResourceHeapImpl* transientHeap,
    CommandBufferImpl* cmdBuffer
)
{
    CommandEncoderImpl::init(cmdBuffer);
    m_preCmdList = nullptr;
    m_transientHeap = transientHeap;
    m_currentPipeline = nullptr;
}

Result ComputeCommandEncoderImpl::bindPipeline(IPipeline* state, IShaderObject** outRootObject)
{
    return bindPipelineImpl(state, outRootObject);
}

Result ComputeCommandEncoderImpl::bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject)
{
    return bindPipelineWithRootObjectImpl(state, rootObject);
}

Result ComputeCommandEncoderImpl::dispatchCompute(int x, int y, int z)
{
    // Submit binding for compute
    {
        ComputeSubmitter submitter(m_d3dCmdList);
        RefPtr<PipelineBase> newPipeline;
        SLANG_RETURN_ON_FAIL(_bindRenderState(&submitter, newPipeline));
    }
    m_d3dCmdList->Dispatch(x, y, z);
    return SLANG_OK;
}

Result ComputeCommandEncoderImpl::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    // Submit binding for compute
    {
        ComputeSubmitter submitter(m_d3dCmdList);
        RefPtr<PipelineBase> newPipeline;
        SLANG_RETURN_ON_FAIL(_bindRenderState(&submitter, newPipeline));
    }
    auto argBufferImpl = static_cast<BufferImpl*>(argBuffer);

    m_d3dCmdList->ExecuteIndirect(
        m_renderer->dispatchIndirectCmdSignature,
        1,
        argBufferImpl->m_resource,
        (uint64_t)offset,
        nullptr,
        0
    );
    return SLANG_OK;
}

#if SLANG_RHI_DXR

// RayTracingCommandEncoderImpl

void RayTracingCommandEncoderImpl::buildAccelerationStructure(
    const IAccelerationStructure::BuildDesc& desc,
    GfxCount propertyQueryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    if (!m_commandBuffer->m_cmdList4)
    {
        getDebugCallback()->handleMessage(
            DebugMessageType::Error,
            DebugMessageSource::Layer,
            "Ray-tracing is not supported on current system."
        );
        return;
    }
    AccelerationStructureImpl* destASImpl = nullptr;
    if (desc.dest)
        destASImpl = static_cast<AccelerationStructureImpl*>(desc.dest);
    AccelerationStructureImpl* srcASImpl = nullptr;
    if (desc.source)
        srcASImpl = static_cast<AccelerationStructureImpl*>(desc.source);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.DestAccelerationStructureData = destASImpl->getDeviceAddress();
    buildDesc.SourceAccelerationStructureData = srcASImpl ? srcASImpl->getDeviceAddress() : 0;
    buildDesc.ScratchAccelerationStructureData = desc.scratchData;
    D3DAccelerationStructureInputsBuilder builder;
    builder.build(desc.inputs, getDebugCallback());
    buildDesc.Inputs = builder.desc;

    std::vector<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC> postBuildInfoDescs;
    translatePostBuildInfoDescs(propertyQueryCount, queryDescs, postBuildInfoDescs);
    m_commandBuffer->m_cmdList4
        ->BuildRaytracingAccelerationStructure(&buildDesc, (UINT)propertyQueryCount, postBuildInfoDescs.data());
}

void RayTracingCommandEncoderImpl::copyAccelerationStructure(
    IAccelerationStructure* dest,
    IAccelerationStructure* src,
    AccelerationStructureCopyMode mode
)
{
    auto destASImpl = static_cast<AccelerationStructureImpl*>(dest);
    auto srcASImpl = static_cast<AccelerationStructureImpl*>(src);
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
        getDebugCallback()->handleMessage(
            DebugMessageType::Error,
            DebugMessageSource::Layer,
            "Unsupported AccelerationStructureCopyMode."
        );
        return;
    }
    m_commandBuffer->m_cmdList4
        ->CopyRaytracingAccelerationStructure(destASImpl->getDeviceAddress(), srcASImpl->getDeviceAddress(), copyMode);
}

void RayTracingCommandEncoderImpl::queryAccelerationStructureProperties(
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
    m_commandBuffer->m_cmdList4->EmitRaytracingAccelerationStructurePostbuildInfo(
        postBuildInfoDescs.data(),
        (UINT)accelerationStructureCount,
        asAddresses.data()
    );
}

void RayTracingCommandEncoderImpl::serializeAccelerationStructure(DeviceAddress dest, IAccelerationStructure* src)
{
    auto srcASImpl = static_cast<AccelerationStructureImpl*>(src);
    m_commandBuffer->m_cmdList4->CopyRaytracingAccelerationStructure(
        dest,
        srcASImpl->getDeviceAddress(),
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_SERIALIZE
    );
}

void RayTracingCommandEncoderImpl::deserializeAccelerationStructure(IAccelerationStructure* dest, DeviceAddress source)
{
    auto destASImpl = static_cast<AccelerationStructureImpl*>(dest);
    m_commandBuffer->m_cmdList4->CopyRaytracingAccelerationStructure(
        dest->getDeviceAddress(),
        source,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_DESERIALIZE
    );
}

Result RayTracingCommandEncoderImpl::bindPipeline(IPipeline* state, IShaderObject** outRootObject)
{
    return bindPipelineImpl(state, outRootObject);
}

Result RayTracingCommandEncoderImpl::dispatchRays(
    GfxIndex rayGenShaderIndex,
    IShaderTable* shaderTable,
    GfxCount width,
    GfxCount height,
    GfxCount depth
)
{
    RefPtr<PipelineBase> newPipeline;
    PipelineBase* pipeline = m_currentPipeline.Ptr();
    {
        struct RayTracingSubmitter : public ComputeSubmitter
        {
            ID3D12GraphicsCommandList4* m_cmdList4;
            RayTracingSubmitter(ID3D12GraphicsCommandList4* cmdList4)
                : ComputeSubmitter(cmdList4)
                , m_cmdList4(cmdList4)
            {
            }
            virtual void setPipeline(PipelineBase* pipeline) override
            {
                auto pipelineImpl = static_cast<RayTracingPipelineImpl*>(pipeline);
                m_cmdList4->SetPipelineState1(pipelineImpl->m_stateObject.get());
            }
        };
        RayTracingSubmitter submitter(m_commandBuffer->m_cmdList4);
        SLANG_RETURN_ON_FAIL(_bindRenderState(&submitter, newPipeline));
        if (newPipeline)
            pipeline = newPipeline.Ptr();
    }
    auto pipelineImpl = static_cast<RayTracingPipelineImpl*>(pipeline);

    auto shaderTableImpl = static_cast<ShaderTableImpl*>(shaderTable);

    auto shaderTableBuffer = shaderTableImpl->getOrCreateBuffer(pipelineImpl, m_transientHeap, this);
    auto shaderTableAddr = shaderTableBuffer->getDeviceAddress();

    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};

    dispatchDesc.RayGenerationShaderRecord.StartAddress =
        shaderTableAddr + shaderTableImpl->m_rayGenTableOffset + rayGenShaderIndex * kRayGenRecordSize;
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

    if (shaderTableImpl->m_missShaderCount > 0)
    {
        dispatchDesc.MissShaderTable.StartAddress = shaderTableAddr + shaderTableImpl->m_missTableOffset;
        dispatchDesc.MissShaderTable.SizeInBytes =
            shaderTableImpl->m_missShaderCount * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        dispatchDesc.MissShaderTable.StrideInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    }

    if (shaderTableImpl->m_hitGroupCount > 0)
    {
        dispatchDesc.HitGroupTable.StartAddress = shaderTableAddr + shaderTableImpl->m_hitGroupTableOffset;
        dispatchDesc.HitGroupTable.SizeInBytes =
            shaderTableImpl->m_hitGroupCount * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        dispatchDesc.HitGroupTable.StrideInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    }

    if (shaderTableImpl->m_callableShaderCount > 0)
    {
        dispatchDesc.CallableShaderTable.StartAddress = shaderTableAddr + shaderTableImpl->m_callableTableOffset;
        dispatchDesc.CallableShaderTable.SizeInBytes =
            shaderTableImpl->m_callableShaderCount * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        dispatchDesc.CallableShaderTable.StrideInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    }

    dispatchDesc.Width = (UINT)width;
    dispatchDesc.Height = (UINT)height;
    dispatchDesc.Depth = (UINT)depth;
    m_commandBuffer->m_cmdList4->DispatchRays(&dispatchDesc);

    return SLANG_OK;
}

#endif // SLANG_RHI_DXR

} // namespace rhi::d3d12
