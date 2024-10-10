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
#include "d3d12-texture-view.h"
#include "d3d12-acceleration-structure.h"

#include "core/short_vector.h"

namespace rhi::d3d12 {

// PassEncoderImpl

void PassEncoderImpl::setBufferState(IBuffer* buffer, ResourceState state)
{
    m_commandBuffer->m_stateTracking.setBufferState(checked_cast<BufferImpl*>(buffer), state);
}

void PassEncoderImpl::setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state)
{
    m_commandBuffer->m_stateTracking.setTextureState(checked_cast<TextureImpl*>(texture), subresourceRange, state);
}

void PassEncoderImpl::beginDebugEvent(const char* name, float rgbColor[3])
{
    auto beginEvent = m_commandBuffer->m_device->m_BeginEventOnCommandList;
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

void PassEncoderImpl::endDebugEvent()
{
    auto endEvent = m_commandBuffer->m_device->m_EndEventOnCommandList;
    if (endEvent)
    {
        endEvent(m_commandBuffer->m_cmdList);
    }
}

void PassEncoderImpl::writeTimestamp(IQueryPool* pool, GfxIndex index)
{
    checked_cast<QueryPoolImpl*>(pool)->writeTimestamp(m_commandBuffer->m_cmdList, index);
}


int PassEncoderImpl::getBindPointIndex(PipelineType type)
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

void PassEncoderImpl::init(CommandBufferImpl* commandBuffer)
{
    m_commandBuffer = commandBuffer;
    m_d3dCmdList = m_commandBuffer->m_cmdList;
    m_d3dCmdList6 = m_commandBuffer->m_cmdList6;
    m_device = commandBuffer->m_device;
    m_transientHeap = commandBuffer->m_transientHeap;
    m_d3dDevice = commandBuffer->m_device->m_device;
}

Result PassEncoderImpl::bindPipelineImpl(IPipeline* pipeline, IShaderObject** outRootObject)
{
    m_currentPipeline = checked_cast<Pipeline*>(pipeline);
    auto rootObject = &m_commandBuffer->m_rootShaderObject;
    m_commandBuffer->m_mutableRootShaderObject = nullptr;
    SLANG_RETURN_ON_FAIL(rootObject->reset(
        m_device,
        m_currentPipeline->getProgram<ShaderProgramImpl>()->m_rootObjectLayout,
        m_commandBuffer->m_transientHeap
    ));
    *outRootObject = rootObject;
    m_bindingDirty = true;
    return SLANG_OK;
}

Result PassEncoderImpl::bindPipelineWithRootObjectImpl(IPipeline* pipeline, IShaderObject* rootObject)
{
    m_currentPipeline = checked_cast<Pipeline*>(pipeline);
    m_commandBuffer->m_mutableRootShaderObject = checked_cast<MutableRootShaderObjectImpl*>(rootObject);
    m_bindingDirty = true;
    return SLANG_OK;
}

Result PassEncoderImpl::_bindRenderState(Submitter* submitter, RefPtr<Pipeline>& newPipeline)
{
    RootShaderObjectImpl* rootObjectImpl = m_commandBuffer->m_mutableRootShaderObject
                                               ? m_commandBuffer->m_mutableRootShaderObject.Ptr()
                                               : &m_commandBuffer->m_rootShaderObject;
    SLANG_RETURN_ON_FAIL(m_device->maybeSpecializePipeline(m_currentPipeline, rootObjectImpl, newPipeline));
    Pipeline* newPipelineImpl = newPipeline.get();
    auto commandList = m_d3dCmdList;
    auto pipelineTypeIndex = (int)newPipelineImpl->desc.type;
    auto programImpl = checked_cast<ShaderProgramImpl*>(newPipelineImpl->m_program.Ptr());
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
    context.device = m_device;
    context.transientHeap = m_transientHeap;
    context.outOfMemoryHeap = (D3D12_DESCRIPTOR_HEAP_TYPE)(-1);

    // Transition all resources to the appropriate state and commit the barriers.
    // This needs to happen before binding descriptor tables, otherwise D3D12
    // will report validation errors.
    rootObjectImpl->setResourceStates(m_commandBuffer->m_stateTracking);
    m_commandBuffer->commitBarriers();

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
            SLANG_RETURN_ON_FAIL(m_transientHeap->allocateNewViewDescriptorHeap(m_device));
            d3dheap = m_transientHeap->getCurrentViewHeap().getHeap();
            m_commandBuffer->bindDescriptorHeaps();
            break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            SLANG_RETURN_ON_FAIL(m_transientHeap->allocateNewSamplerDescriptorHeap(m_device));
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

// ResourcePassEncoderImpl

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
    TextureImpl* dstTexture = checked_cast<TextureImpl*>(dst);
    TextureImpl* srcTexture = checked_cast<TextureImpl*>(src);

    m_commandBuffer->requireTextureState(dstTexture, dstSubresource, ResourceState::CopyDestination);
    m_commandBuffer->requireTextureState(srcTexture, srcSubresource, ResourceState::CopySource);
    m_commandBuffer->commitBarriers();

    if (dstSubresource.layerCount == 0 && dstSubresource.mipLevelCount == 0 && srcSubresource.layerCount == 0 &&
        srcSubresource.mipLevelCount == 0)
    {
        m_commandBuffer->m_cmdList->CopyResource(
            dstTexture->m_resource.getResource(),
            srcTexture->m_resource.getResource()
        );
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

                m_commandBuffer->m_cmdList
                    ->CopyTextureRegion(&dstRegion, dstOffset.x, dstOffset.y, dstOffset.z, &srcRegion, &srcBox);
            }
        }
    }
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
    TextureImpl* dstTexture = checked_cast<TextureImpl*>(dst);

    m_commandBuffer->requireTextureState(dstTexture, subresourceRange, ResourceState::CopyDestination);
    m_commandBuffer->commitBarriers();

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
        m_commandBuffer->m_cmdList->CopyTextureRegion(&dstRegion, offset.x, offset.y, offset.z, &srcRegion, nullptr);
    }
}

void ResourcePassEncoderImpl::clearBuffer(IBuffer* buffer, const BufferRange* range)
{
    // TODO implement
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
    SLANG_RHI_UNIMPLEMENTED("clearTexture");
}

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

void ResourcePassEncoderImpl::resolveQuery(
    IQueryPool* queryPool,
    GfxIndex index,
    GfxCount count,
    IBuffer* buffer,
    Offset offset
)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);

    m_commandBuffer->requireBufferState(bufferImpl, ResourceState::CopyDestination);
    m_commandBuffer->commitBarriers();

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
        auto queryPoolImpl = checked_cast<QueryPoolImpl*>(queryPool);
        auto bufferImpl = checked_cast<BufferImpl*>(buffer);
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
    SLANG_RHI_ASSERT(srcSubresource.mipLevelCount <= 1);

    BufferImpl* dstBuffer = checked_cast<BufferImpl*>(dst);
    TextureImpl* srcTexture = checked_cast<TextureImpl*>(src);

    m_commandBuffer->requireBufferState(dstBuffer, ResourceState::CopyDestination);
    m_commandBuffer->requireTextureState(srcTexture, srcSubresource, ResourceState::CopySource);
    m_commandBuffer->commitBarriers();

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

void ResourcePassEncoderImpl::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    BufferImpl* dstBuffer = checked_cast<BufferImpl*>(dst);
    BufferImpl* srcBuffer = checked_cast<BufferImpl*>(src);

    m_commandBuffer->requireBufferState(dstBuffer, ResourceState::CopyDestination);
    m_commandBuffer->requireBufferState(srcBuffer, ResourceState::CopySource);
    m_commandBuffer->commitBarriers();

    m_commandBuffer->m_cmdList->CopyBufferRegion(
        dstBuffer->m_resource.getResource(),
        dstOffset,
        srcBuffer->m_resource.getResource(),
        srcOffset,
        size
    );
}

void ResourcePassEncoderImpl::uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data)
{
    BufferImpl* dstBuffer = checked_cast<BufferImpl*>(dst);

    m_commandBuffer->requireBufferState(dstBuffer, ResourceState::CopyDestination);
    m_commandBuffer->commitBarriers();

    uploadBufferDataImpl(
        m_commandBuffer->m_device->m_device,
        m_commandBuffer->m_cmdList,
        m_commandBuffer->m_transientHeap,
        dstBuffer,
        offset,
        size,
        data
    );
}

// RenderPassEncoderImpl

void RenderPassEncoderImpl::init(
    DeviceImpl* device,
    TransientResourceHeapImpl* transientHeap,
    CommandBufferImpl* cmdBuffer,
    const RenderPassDesc& desc
)
{
    PassEncoderImpl::init(cmdBuffer);
    m_preCmdList = nullptr;
    m_transientHeap = transientHeap;
    m_boundVertexBuffers.clear();
    m_boundIndexBuffer = nullptr;
    m_boundIndexFormat = DXGI_FORMAT_UNKNOWN;
    m_boundIndexOffset = 0;
    m_currentPipeline = nullptr;

    m_renderTargetViews.resize(desc.colorAttachmentCount);
    m_resolveTargetViews.resize(desc.colorAttachmentCount);
    short_vector<D3D12_CPU_DESCRIPTOR_HANDLE> renderTargetDescriptors;
    for (Index i = 0; i < desc.colorAttachmentCount; i++)
    {
        m_renderTargetViews[i] = checked_cast<TextureViewImpl*>(desc.colorAttachments[i].view);
        m_resolveTargetViews[i] = checked_cast<TextureViewImpl*>(desc.colorAttachments[i].resolveTarget);
        m_commandBuffer->requireTextureState(
            m_renderTargetViews[i]->m_texture,
            m_renderTargetViews[i]->m_desc.subresourceRange,
            ResourceState::RenderTarget
        );
        renderTargetDescriptors.push_back(m_renderTargetViews[i]->getRTV().cpuHandle);
    }
    if (desc.depthStencilAttachment)
    {
        m_depthStencilView = checked_cast<TextureViewImpl*>(desc.depthStencilAttachment->view);
        m_commandBuffer->requireTextureState(
            m_depthStencilView->m_texture,
            m_depthStencilView->m_desc.subresourceRange,
            desc.depthStencilAttachment->depthReadOnly ? ResourceState::DepthRead : ResourceState::DepthWrite
        );
    }

    m_commandBuffer->commitBarriers();

    m_d3dCmdList->OMSetRenderTargets(
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
            m_d3dCmdList->ClearRenderTargetView(renderTargetDescriptors[i], attachment.clearValue, 0, nullptr);
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
            m_d3dCmdList->ClearDepthStencilView(
                m_depthStencilView->getDSV().cpuHandle,
                (D3D12_CLEAR_FLAGS)clearFlags,
                attachment.depthClearValue,
                attachment.stencilClearValue,
                0,
                nullptr
            );
        }
    }
}

Result RenderPassEncoderImpl::bindPipeline(IPipeline* state, IShaderObject** outRootObject)
{
    return bindPipelineImpl(state, outRootObject);
}

Result RenderPassEncoderImpl::bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject)
{
    return bindPipelineWithRootObjectImpl(state, rootObject);
}

void RenderPassEncoderImpl::setViewports(GfxCount count, const Viewport* viewports)
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

void RenderPassEncoderImpl::setScissorRects(GfxCount count, const ScissorRect* rects)
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

void RenderPassEncoderImpl::setVertexBuffers(
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
        BoundVertexBuffer& boundBuffer = m_boundVertexBuffers[startSlot + i];
        boundBuffer.m_buffer = checked_cast<BufferImpl*>(buffers[i]);
        boundBuffer.m_offset = int(offsets[i]);
    }
}

void RenderPassEncoderImpl::setIndexBuffer(IBuffer* buffer, IndexFormat indexFormat, Offset offset)
{
    m_boundIndexBuffer = checked_cast<BufferImpl*>(buffer);
    m_boundIndexFormat = D3DUtil::getIndexFormat(indexFormat);
    m_boundIndexOffset = (UINT)offset;
}

Result RenderPassEncoderImpl::prepareDraw()
{
    Pipeline* pipeline = m_currentPipeline.get();
    if (!pipeline || (pipeline->desc.type != PipelineType::Graphics))
    {
        return SLANG_FAIL;
    }
    InputLayoutImpl* inputLayout = (InputLayoutImpl*)pipeline->inputLayout.get();

    if (inputLayout)
    {
        for (Index i = 0; i < m_boundVertexBuffers.size(); i++)
        {
            m_commandBuffer->requireBufferState(m_boundVertexBuffers[i].m_buffer, ResourceState::VertexBuffer);
        }
    }
    if (m_boundIndexBuffer)
    {
        m_commandBuffer->requireBufferState(m_boundIndexBuffer, ResourceState::IndexBuffer);
    }

    // Submit - setting for graphics
    {
        GraphicsSubmitter submitter(m_d3dCmdList);
        RefPtr<Pipeline> newPipeline;
        SLANG_RETURN_ON_FAIL(_bindRenderState(&submitter, newPipeline));
    }

    m_d3dCmdList->IASetPrimitiveTopology(D3DUtil::getPrimitiveTopology(pipeline->desc.graphics.primitiveTopology));

    // Set up vertex buffer views
    {
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
                    vertexView.SizeInBytes = UINT(buffer->m_desc.size - boundVertexBuffer.m_offset);
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
        indexBufferView.SizeInBytes = UINT(m_boundIndexBuffer->m_desc.size - m_boundIndexOffset);
        indexBufferView.Format = m_boundIndexFormat;

        m_d3dCmdList->IASetIndexBuffer(&indexBufferView);
    }

    return SLANG_OK;
}

Result RenderPassEncoderImpl::draw(GfxCount vertexCount, GfxIndex startVertex)
{
    SLANG_RETURN_ON_FAIL(prepareDraw());
    m_d3dCmdList->DrawInstanced((uint32_t)vertexCount, 1, (uint32_t)startVertex, 0);
    return SLANG_OK;
}

Result RenderPassEncoderImpl::drawIndexed(GfxCount indexCount, GfxIndex startIndex, GfxIndex baseVertex)
{
    SLANG_RETURN_ON_FAIL(prepareDraw());
    m_d3dCmdList->DrawIndexedInstanced((uint32_t)indexCount, 1, (uint32_t)startIndex, (uint32_t)baseVertex, 0);
    return SLANG_OK;
}

void RenderPassEncoderImpl::end()
{
    bool needsResolve = false;
    for (size_t i = 0; i < m_renderTargetViews.size(); ++i)
    {
        if (m_renderTargetViews[i] && m_resolveTargetViews[i])
        {
            m_commandBuffer->requireTextureState(
                m_renderTargetViews[i]->m_texture,
                m_renderTargetViews[i]->m_desc.subresourceRange,
                ResourceState::ResolveSource
            );
            m_commandBuffer->requireTextureState(
                m_resolveTargetViews[i]->m_texture,
                m_resolveTargetViews[i]->m_desc.subresourceRange,
                ResourceState::ResolveDestination
            );
            needsResolve = true;
        }
    }

    if (needsResolve)
    {
        m_commandBuffer->commitBarriers();

        for (size_t i = 0; i < m_renderTargetViews.size(); ++i)
        {
            if (m_renderTargetViews[i] && m_resolveTargetViews[i])
            {
                TextureViewImpl* srcView = m_renderTargetViews[i].get();
                TextureViewImpl* dstView = m_resolveTargetViews[i].get();
                DXGI_FORMAT format = D3DUtil::getMapFormat(srcView->m_texture->m_desc.format);
                m_commandBuffer->m_cmdList->ResolveSubresource(
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

    PassEncoderImpl::endEncodingImpl();
}

void RenderPassEncoderImpl::setStencilReference(uint32_t referenceValue)
{
    m_d3dCmdList->OMSetStencilRef((UINT)referenceValue);
}

Result RenderPassEncoderImpl::drawIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    BufferImpl* argBufferImpl = checked_cast<BufferImpl*>(argBuffer);
    BufferImpl* countBufferImpl = checked_cast<BufferImpl*>(countBuffer);

    m_commandBuffer->requireBufferState(argBufferImpl, ResourceState::IndirectArgument);
    if (countBufferImpl)
    {
        m_commandBuffer->requireBufferState(countBufferImpl, ResourceState::IndirectArgument);
    }

    SLANG_RETURN_ON_FAIL(prepareDraw());

    m_d3dCmdList->ExecuteIndirect(
        m_device->drawIndirectCmdSignature,
        (uint32_t)maxDrawCount,
        argBufferImpl->m_resource,
        (uint64_t)argOffset,
        countBufferImpl ? countBufferImpl->m_resource.getResource() : nullptr,
        (uint64_t)countOffset
    );
    return SLANG_OK;
}

Result RenderPassEncoderImpl::drawIndexedIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    BufferImpl* argBufferImpl = checked_cast<BufferImpl*>(argBuffer);
    BufferImpl* countBufferImpl = checked_cast<BufferImpl*>(countBuffer);

    m_commandBuffer->requireBufferState(argBufferImpl, ResourceState::IndirectArgument);
    if (countBufferImpl)
    {
        m_commandBuffer->requireBufferState(countBufferImpl, ResourceState::IndirectArgument);
    }

    SLANG_RETURN_ON_FAIL(prepareDraw());

    m_d3dCmdList->ExecuteIndirect(
        m_device->drawIndexedIndirectCmdSignature,
        (uint32_t)maxDrawCount,
        argBufferImpl->m_resource,
        (uint64_t)argOffset,
        countBufferImpl ? countBufferImpl->m_resource.getResource() : nullptr,
        (uint64_t)countOffset
    );

    return SLANG_OK;
}

Result RenderPassEncoderImpl::setSamplePositions(
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

Result RenderPassEncoderImpl::drawInstanced(
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

Result RenderPassEncoderImpl::drawIndexedInstanced(
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

Result RenderPassEncoderImpl::drawMeshTasks(int x, int y, int z)
{
    SLANG_RETURN_ON_FAIL(prepareDraw());
    m_d3dCmdList6->DispatchMesh(x, y, z);
    return SLANG_OK;
}

// ComputePassEncoderImpl

void ComputePassEncoderImpl::end()
{
    PassEncoderImpl::endEncodingImpl();
}

void ComputePassEncoderImpl::init(
    DeviceImpl* device,
    TransientResourceHeapImpl* transientHeap,
    CommandBufferImpl* cmdBuffer
)
{
    PassEncoderImpl::init(cmdBuffer);
    m_preCmdList = nullptr;
    m_transientHeap = transientHeap;
    m_currentPipeline = nullptr;
}

Result ComputePassEncoderImpl::bindPipeline(IPipeline* state, IShaderObject** outRootObject)
{
    return bindPipelineImpl(state, outRootObject);
}

Result ComputePassEncoderImpl::bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject)
{
    return bindPipelineWithRootObjectImpl(state, rootObject);
}

Result ComputePassEncoderImpl::dispatchCompute(int x, int y, int z)
{
    // Submit binding for compute
    {
        ComputeSubmitter submitter(m_d3dCmdList);
        RefPtr<Pipeline> newPipeline;
        SLANG_RETURN_ON_FAIL(_bindRenderState(&submitter, newPipeline));
    }
    m_d3dCmdList->Dispatch(x, y, z);
    return SLANG_OK;
}

Result ComputePassEncoderImpl::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    // Submit binding for compute
    {
        ComputeSubmitter submitter(m_d3dCmdList);
        RefPtr<Pipeline> newPipeline;
        SLANG_RETURN_ON_FAIL(_bindRenderState(&submitter, newPipeline));
    }

    BufferImpl* argBufferImpl = checked_cast<BufferImpl*>(argBuffer);

    m_commandBuffer->requireBufferState(argBufferImpl, ResourceState::IndirectArgument);
    m_commandBuffer->commitBarriers();

    m_d3dCmdList->ExecuteIndirect(
        m_device->dispatchIndirectCmdSignature,
        1,
        argBufferImpl->m_resource,
        (uint64_t)offset,
        nullptr,
        0
    );
    return SLANG_OK;
}

#if SLANG_RHI_DXR

// RayTracingPassEncoderImpl

void RayTracingPassEncoderImpl::buildAccelerationStructure(
    const AccelerationStructureBuildDesc& desc,
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    BufferWithOffset scratchBuffer,
    GfxCount propertyQueryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    if (!m_commandBuffer->m_cmdList4)
    {
        m_device->handleMessage(
            DebugMessageType::Error,
            DebugMessageSource::Layer,
            "Ray-tracing is not supported on current system."
        );
        return;
    }
    AccelerationStructureImpl* dstImpl = checked_cast<AccelerationStructureImpl*>(dst);
    AccelerationStructureImpl* srcImpl = checked_cast<AccelerationStructureImpl*>(src);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.DestAccelerationStructureData = dstImpl->getDeviceAddress();
    buildDesc.SourceAccelerationStructureData = srcImpl ? srcImpl->getDeviceAddress() : 0;
    buildDesc.ScratchAccelerationStructureData = scratchBuffer.buffer->getDeviceAddress() + scratchBuffer.offset;
    D3DAccelerationStructureInputsBuilder builder;
    builder.build(desc, m_device->m_debugCallback);
    buildDesc.Inputs = builder.desc;

    std::vector<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC> postBuildInfoDescs;
    translatePostBuildInfoDescs(propertyQueryCount, queryDescs, postBuildInfoDescs);
    m_commandBuffer->m_cmdList4
        ->BuildRaytracingAccelerationStructure(&buildDesc, (UINT)propertyQueryCount, postBuildInfoDescs.data());
}

void RayTracingPassEncoderImpl::copyAccelerationStructure(
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
    m_commandBuffer->m_cmdList4
        ->CopyRaytracingAccelerationStructure(dstImpl->getDeviceAddress(), srcImpl->getDeviceAddress(), copyMode);
}

void RayTracingPassEncoderImpl::queryAccelerationStructureProperties(
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

void RayTracingPassEncoderImpl::serializeAccelerationStructure(BufferWithOffset dst, IAccelerationStructure* src)
{
    auto srcImpl = checked_cast<AccelerationStructureImpl*>(src);
    m_commandBuffer->m_cmdList4->CopyRaytracingAccelerationStructure(
        checked_cast<BufferImpl*>(dst.buffer)->getDeviceAddress() + dst.offset,
        srcImpl->getDeviceAddress(),
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_SERIALIZE
    );
}

void RayTracingPassEncoderImpl::deserializeAccelerationStructure(IAccelerationStructure* dst, BufferWithOffset src)
{
    auto dstImpl = checked_cast<AccelerationStructureImpl*>(dst);
    m_commandBuffer->m_cmdList4->CopyRaytracingAccelerationStructure(
        dstImpl->getDeviceAddress(),
        checked_cast<BufferImpl*>(src.buffer)->getDeviceAddress() + src.offset,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_DESERIALIZE
    );
}

Result RayTracingPassEncoderImpl::bindPipeline(IPipeline* state, IShaderObject** outRootObject)
{
    return bindPipelineImpl(state, outRootObject);
}

Result RayTracingPassEncoderImpl::dispatchRays(
    GfxIndex rayGenShaderIndex,
    IShaderTable* shaderTable,
    GfxCount width,
    GfxCount height,
    GfxCount depth
)
{
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
                auto pipelineImpl = checked_cast<RayTracingPipelineImpl*>(pipeline);
                m_cmdList4->SetPipelineState1(pipelineImpl->m_stateObject.get());
            }
        };
        RayTracingSubmitter submitter(m_commandBuffer->m_cmdList4);
        SLANG_RETURN_ON_FAIL(_bindRenderState(&submitter, newPipeline));
        if (newPipeline)
            pipeline = newPipeline.Ptr();
    }
    auto pipelineImpl = checked_cast<RayTracingPipelineImpl*>(pipeline);

    auto shaderTableImpl = checked_cast<ShaderTableImpl*>(shaderTable);

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
