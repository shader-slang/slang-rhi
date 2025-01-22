#include "d3d12-command.h"
#include "d3d12-device.h"
#include "d3d12-buffer.h"
#include "d3d12-texture.h"
#include "d3d12-acceleration-structure.h"
#include "d3d12-pipeline.h"
#include "d3d12-shader-table.h"
#include "d3d12-shader-object.h"
#include "d3d12-shader-object-layout.h"
#include "d3d12-fence.h"
#include "d3d12-query.h"
#include "d3d12-input-layout.h"
#include "../state-tracking.h"
#include "../strings.h"

#include "core/short_vector.h"

namespace rhi::d3d12 {

template<typename T>
inline bool arraysEqual(GfxCount countA, GfxCount countB, const T* a, const T* b)
{
    return (countA == countB) ? std::memcmp(a, b, countA * sizeof(T)) == 0 : false;
}

class CommandRecorder
{
public:
    DeviceImpl* m_device;

    ComPtr<ID3D12GraphicsCommandList> m_cmdList;
    ComPtr<ID3D12GraphicsCommandList1> m_cmdList1;
    ComPtr<ID3D12GraphicsCommandList4> m_cmdList4;
    ComPtr<ID3D12GraphicsCommandList6> m_cmdList6;

    D3D12DescriptorHeap* m_viewDescriptorHeap;
    D3D12DescriptorHeap* m_samplerDescriptorHeap;

    BufferPool<DeviceImpl, BufferImpl>* m_constantBufferPool;
    BufferPool<DeviceImpl, BufferImpl>* m_uploadBufferPool;

    bool m_descriptorHeapsBound = false;

    StateTracking m_stateTracking;

    short_vector<RefPtr<TextureViewImpl>> m_renderTargetViews;
    short_vector<RefPtr<TextureViewImpl>> m_resolveTargetViews;
    RefPtr<TextureViewImpl> m_depthStencilView;

    bool m_renderPassActive = false;
    bool m_renderStateValid = false;
    RenderState m_renderState;
    RefPtr<RenderPipelineImpl> m_renderPipeline;

    bool m_computePassActive = false;
    bool m_computeStateValid = false;
    ComputeState m_computeState;
    RefPtr<ComputePipelineImpl> m_computePipeline;

    bool m_rayTracingPassActive = false;
    bool m_rayTracingStateValid = false;
    RayTracingState m_rayTracingState;
    RefPtr<RayTracingPipelineImpl> m_rayTracingPipeline;
    RefPtr<ShaderTableImpl> m_shaderTable;
    D3D12_DISPATCH_RAYS_DESC m_dispatchRaysDesc = {};
    UINT64 m_rayGenTableAddr = 0;

    CommandRecorder(DeviceImpl* device)
        : m_device(device)
    {
    }

    Result record(CommandBufferImpl* commandBuffer);

    void cmdCopyBuffer(const commands::CopyBuffer& cmd);
    void cmdCopyTexture(const commands::CopyTexture& cmd);
    void cmdCopyTextureToBuffer(const commands::CopyTextureToBuffer& cmd);
    void cmdClearBuffer(const commands::ClearBuffer& cmd);
    void cmdClearTexture(const commands::ClearTexture& cmd);
    void cmdUploadTextureData(const commands::UploadTextureData& cmd);
    void cmdUploadBufferData(const commands::UploadBufferData& cmd);
    void cmdResolveQuery(const commands::ResolveQuery& cmd);
    void cmdBeginRenderPass(const commands::BeginRenderPass& cmd);
    void cmdEndRenderPass(const commands::EndRenderPass& cmd);
    void cmdSetRenderState(const commands::SetRenderState& cmd);
    void cmdDraw(const commands::Draw& cmd);
    void cmdDrawIndexed(const commands::DrawIndexed& cmd);
    void cmdDrawIndirect(const commands::DrawIndirect& cmd);
    void cmdDrawIndexedIndirect(const commands::DrawIndexedIndirect& cmd);
    void cmdDrawMeshTasks(const commands::DrawMeshTasks& cmd);
    void cmdBeginComputePass(const commands::BeginComputePass& cmd);
    void cmdEndComputePass(const commands::EndComputePass& cmd);
    void cmdSetComputeState(const commands::SetComputeState& cmd);
    void cmdDispatchCompute(const commands::DispatchCompute& cmd);
    void cmdDispatchComputeIndirect(const commands::DispatchComputeIndirect& cmd);
    void cmdBeginRayTracingPass(const commands::BeginRayTracingPass& cmd);
    void cmdEndRayTracingPass(const commands::EndRayTracingPass& cmd);
    void cmdSetRayTracingState(const commands::SetRayTracingState& cmd);
    void cmdDispatchRays(const commands::DispatchRays& cmd);
    void cmdBuildAccelerationStructure(const commands::BuildAccelerationStructure& cmd);
    void cmdCopyAccelerationStructure(const commands::CopyAccelerationStructure& cmd);
    void cmdQueryAccelerationStructureProperties(const commands::QueryAccelerationStructureProperties& cmd);
    void cmdSerializeAccelerationStructure(const commands::SerializeAccelerationStructure& cmd);
    void cmdDeserializeAccelerationStructure(const commands::DeserializeAccelerationStructure& cmd);
    void cmdSetBufferState(const commands::SetBufferState& cmd);
    void cmdSetTextureState(const commands::SetTextureState& cmd);
    void cmdPushDebugGroup(const commands::PushDebugGroup& cmd);
    void cmdPopDebugGroup(const commands::PopDebugGroup& cmd);
    void cmdInsertDebugMarker(const commands::InsertDebugMarker& cmd);
    void cmdWriteTimestamp(const commands::WriteTimestamp& cmd);
    void cmdExecuteCallback(const commands::ExecuteCallback& cmd);

    Result bindRootObject(
        RootShaderObjectImpl* rootObject,
        RootShaderObjectLayoutImpl* rootObjectLayout,
        Submitter* submitter
    );

    void bindDescriptorHeaps();
    void invalidateDescriptorHeapBinding() { m_descriptorHeapsBound = false; }

    void requireBufferState(BufferImpl* buffer, ResourceState state);
    void requireTextureState(TextureImpl* texture, SubresourceRange subresourceRange, ResourceState state);
    void commitBarriers();
};

Result CommandRecorder::record(CommandBufferImpl* commandBuffer)
{
    m_cmdList = commandBuffer->m_d3dCommandList;
    m_cmdList->QueryInterface<ID3D12GraphicsCommandList1>(m_cmdList1.writeRef());
    m_cmdList->QueryInterface<ID3D12GraphicsCommandList4>(m_cmdList4.writeRef());
    m_cmdList->QueryInterface<ID3D12GraphicsCommandList6>(m_cmdList6.writeRef());
    m_viewDescriptorHeap = &commandBuffer->m_viewDescriptorHeap;
    m_samplerDescriptorHeap = &commandBuffer->m_samplerDescriptorHeap;
    m_constantBufferPool = &commandBuffer->m_constantBufferPool;
    m_uploadBufferPool = &commandBuffer->m_uploadBufferPool;

    CommandList* commandList = commandBuffer->m_commandList;

#if 0
    // First, we setup all the root objects.
    for (const CommandList::CommandSlot* slot = commandList->getCommands(); slot; slot = slot->next)
    {
        IShaderObject* rootObject = nullptr;
        switch (slot->id)
        {
        case CommandID::SetRenderState:
        {
            const auto& cmd = commandList->getCommand<commands::SetRenderState>(slot);
            SLANG_RETURN_ON_FAIL(prepareRootObject(
                checked_cast<RootShaderObjectImpl*>(cmd.state.rootObject),
                checked_cast<RenderPipelineImpl*>(cmd.state.pipeline)->m_rootObjectLayout
            ));
            break;
        }
        case CommandID::SetComputeState:
        {
            const auto& cmd = commandList->getCommand<commands::SetComputeState>(slot);
            SLANG_RETURN_ON_FAIL(prepareRootObject(
                checked_cast<RootShaderObjectImpl*>(cmd.state.rootObject),
                checked_cast<ComputePipelineImpl*>(cmd.state.pipeline)->m_rootObjectLayout
            ));
            break;
        }
        case CommandID::SetRayTracingState:
        {
            const auto& cmd = commandList->getCommand<commands::SetRayTracingState>(slot);
            SLANG_RETURN_ON_FAIL(prepareRootObject(
                checked_cast<RootShaderObjectImpl*>(cmd.state.rootObject),
                checked_cast<RayTracingPipelineImpl*>(cmd.state.pipeline)->m_rootObjectLayout
            ));
            break;
        }
        }
    }
#endif

    for (const CommandList::CommandSlot* slot = commandList->getCommands(); slot; slot = slot->next)
    {
#define SLANG_RHI_COMMAND_EXECUTE_X(x)                                                                                 \
    case CommandID::x:                                                                                                 \
        cmd##x(commandList->getCommand<commands::x>(slot));                                                            \
        break;

        switch (slot->id)
        {
            SLANG_RHI_COMMANDS(SLANG_RHI_COMMAND_EXECUTE_X);
        }

#undef SLANG_RHI_COMMAND_EXECUTE_X
    }

    // Transition all resources back to their default states.
    m_stateTracking.requireDefaultStates();
    commitBarriers();
    m_stateTracking.clear();

    SLANG_RETURN_ON_FAIL(m_cmdList->Close());

    return SLANG_OK;
}

#define NOT_SUPPORTED(x) m_device->warning(S_CommandEncoder_##x " command is not supported!")

void CommandRecorder::cmdCopyBuffer(const commands::CopyBuffer& cmd)
{
    BufferImpl* dst = checked_cast<BufferImpl*>(cmd.dst);
    BufferImpl* src = checked_cast<BufferImpl*>(cmd.src);

    requireBufferState(dst, ResourceState::CopyDestination);
    requireBufferState(src, ResourceState::CopySource);
    commitBarriers();

    m_cmdList->CopyBufferRegion(
        dst->m_resource.getResource(),
        cmd.dstOffset,
        src->m_resource.getResource(),
        cmd.srcOffset,
        cmd.size
    );
}

void CommandRecorder::cmdCopyTexture(const commands::CopyTexture& cmd)
{
    TextureImpl* dst = checked_cast<TextureImpl*>(cmd.dst);
    TextureImpl* src = checked_cast<TextureImpl*>(cmd.src);

    const SubresourceRange& dstSubresource = cmd.dstSubresource;
    const Offset3D& dstOffset = cmd.dstOffset;
    const SubresourceRange& srcSubresource = cmd.srcSubresource;
    const Offset3D& srcOffset = cmd.srcOffset;
    const Extents& extent = cmd.extent;

    requireTextureState(dst, dstSubresource, ResourceState::CopyDestination);
    requireTextureState(src, srcSubresource, ResourceState::CopySource);
    commitBarriers();

    if (dstSubresource.layerCount == 0 && dstSubresource.mipLevelCount == 0 && srcSubresource.layerCount == 0 &&
        srcSubresource.mipLevelCount == 0)
    {
        m_cmdList->CopyResource(dst->m_resource.getResource(), src->m_resource.getResource());
        return;
    }

    DXGI_FORMAT d3dFormat = D3DUtil::getMapFormat(dst->m_desc.format);
    uint32_t planeCount = D3DUtil::getPlaneSliceCount(d3dFormat);
    for (GfxIndex planeIndex = 0; planeIndex < planeCount; planeIndex++)
    {
        for (GfxIndex layer = 0; layer < dstSubresource.layerCount; layer++)
        {
            for (GfxIndex mipLevel = 0; mipLevel < dstSubresource.mipLevelCount; mipLevel++)
            {
                D3D12_TEXTURE_COPY_LOCATION dstRegion = {};

                dstRegion.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dstRegion.pResource = dst->m_resource.getResource();
                dstRegion.SubresourceIndex = D3DUtil::getSubresourceIndex(
                    dstSubresource.mipLevel + mipLevel,
                    dstSubresource.baseArrayLayer + layer,
                    planeIndex,
                    dst->m_desc.mipLevelCount,
                    dst->m_desc.arrayLength
                );

                D3D12_TEXTURE_COPY_LOCATION srcRegion = {};
                srcRegion.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                srcRegion.pResource = src->m_resource.getResource();
                srcRegion.SubresourceIndex = D3DUtil::getSubresourceIndex(
                    srcSubresource.mipLevel + mipLevel,
                    srcSubresource.baseArrayLayer + layer,
                    planeIndex,
                    src->m_desc.mipLevelCount,
                    src->m_desc.arrayLength
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

void CommandRecorder::cmdCopyTextureToBuffer(const commands::CopyTextureToBuffer& cmd)
{
    SLANG_RHI_ASSERT(cmd.srcSubresource.mipLevelCount <= 1);

    BufferImpl* dst = checked_cast<BufferImpl*>(cmd.dst);
    TextureImpl* src = checked_cast<TextureImpl*>(cmd.src);

    const Offset dstOffset = cmd.dstOffset;
    const Size dstRowStride = cmd.dstRowStride;

    SubresourceRange srcSubresource = cmd.srcSubresource;
    const Offset3D& srcOffset = cmd.srcOffset;
    const Extents& extent = cmd.extent;

    requireBufferState(dst, ResourceState::CopyDestination);
    requireTextureState(src, srcSubresource, ResourceState::CopySource);
    commitBarriers();

    auto baseSubresourceIndex = D3DUtil::getSubresourceIndex(
        srcSubresource.mipLevel,
        srcSubresource.baseArrayLayer,
        0,
        src->m_desc.mipLevelCount,
        src->m_desc.arrayLength
    );
    auto textureSize = src->m_desc.size;
    const FormatInfo& formatInfo = getFormatInfo(src->m_desc.format);
    if (srcSubresource.mipLevelCount == 0)
        srcSubresource.mipLevelCount = src->m_desc.mipLevelCount;
    if (srcSubresource.layerCount == 0)
        srcSubresource.layerCount = src->m_desc.arrayLength;

    for (GfxCount layer = 0; layer < srcSubresource.layerCount; layer++)
    {
        // Get the footprint
        D3D12_RESOURCE_DESC texDesc = src->m_resource.getResource()->GetDesc();

        D3D12_TEXTURE_COPY_LOCATION dstRegion = {};
        dstRegion.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstRegion.pResource = dst->m_resource.getResource();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint = dstRegion.PlacedFootprint;

        D3D12_TEXTURE_COPY_LOCATION srcRegion = {};
        srcRegion.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcRegion.SubresourceIndex = D3DUtil::getSubresourceIndex(
            srcSubresource.mipLevel,
            layer + srcSubresource.baseArrayLayer,
            0,
            src->m_desc.mipLevelCount,
            src->m_desc.arrayLength
        );
        srcRegion.pResource = src->m_resource.getResource();

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

void CommandRecorder::cmdClearBuffer(const commands::ClearBuffer& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(clearBuffer);
}

void CommandRecorder::cmdClearTexture(const commands::ClearTexture& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(clearTexture);
}

void CommandRecorder::cmdUploadTextureData(const commands::UploadTextureData& cmd)
{
    m_device->warning("uploadTextureData command not implemented");
#if 0
    TextureImpl* dst = checked_cast<TextureImpl*>(cmd.dst);

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
#endif
}

void CommandRecorder::cmdUploadBufferData(const commands::UploadBufferData& cmd)
{
    m_device->warning("uploadBufferData command not implemented");
#if 0
    BufferImpl* dstBuffer = checked_cast<BufferImpl*>(dst);

    requireBufferState(dstBuffer, ResourceState::CopyDestination);
    commitBarriers();

    uploadBufferDataImpl(m_device->m_device, m_cmdList, m_transientHeap, dstBuffer, offset, size, data);
#endif
}

void CommandRecorder::cmdResolveQuery(const commands::ResolveQuery& cmd)
{
    BufferImpl* buffer = checked_cast<BufferImpl*>(cmd.buffer);
    QueryPool* queryPool = checked_cast<QueryPool*>(cmd.queryPool);

    requireBufferState(buffer, ResourceState::CopyDestination);
    commitBarriers();

    switch (queryPool->m_desc.type)
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
            buffer->m_resource.getResource(),
            (uint64_t)cmd.offset,
            srcQueryBuffer,
            cmd.index * sizeof(uint64_t),
            cmd.count * sizeof(uint64_t)
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
        m_cmdList->ResolveQueryData(
            queryPoolImpl->m_queryHeap.get(),
            queryPoolImpl->m_queryType,
            cmd.index,
            cmd.count,
            buffer->m_resource.getResource(),
            cmd.offset
        );
    }
    break;
    }
}

void CommandRecorder::cmdBeginRenderPass(const commands::BeginRenderPass& cmd)
{
    const RenderPassDesc& desc = cmd.desc;

    m_renderTargetViews.resize(desc.colorAttachmentCount);
    m_resolveTargetViews.resize(desc.colorAttachmentCount);
    short_vector<D3D12_CPU_DESCRIPTOR_HANDLE> renderTargetDescriptors;
    for (uint32_t i = 0; i < desc.colorAttachmentCount; i++)
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

void CommandRecorder::cmdEndRenderPass(const commands::EndRenderPass& cmd)
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

void CommandRecorder::cmdSetRenderState(const commands::SetRenderState& cmd)
{
    if (!m_renderPassActive)
        return;

    const RenderState& state = cmd.state;

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
        auto rootObject = checked_cast<RootShaderObjectImpl*>(state.rootObject);
        GraphicsSubmitter submitter(m_device->m_device, m_cmdList);
        if (bindRootObject(rootObject, m_renderPipeline->m_rootObjectLayout, &submitter) != SLANG_OK)
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
        for (uint32_t i = 0; i < state.vertexBufferCount; ++i)
        {
            BufferImpl* buffer = checked_cast<BufferImpl*>(state.vertexBuffers[i].buffer);
            Offset offset = state.vertexBuffers[i].offset;
            requireBufferState(buffer, ResourceState::VertexBuffer);

            D3D12_VERTEX_BUFFER_VIEW& vertexView = vertexViews[i];
            vertexView.BufferLocation = buffer->m_resource.getResource()->GetGPUVirtualAddress() + offset;
            vertexView.SizeInBytes = UINT(buffer->m_desc.size - offset);
            vertexView.StrideInBytes = m_renderPipeline->m_inputLayout->m_vertexStreamStrides[i];
        }
        m_cmdList->IASetVertexBuffers(0, state.vertexBufferCount, vertexViews);
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
        static const uint32_t kMaxViewports = D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        SLANG_RHI_ASSERT(state.viewportCount <= kMaxViewports);
        D3D12_VIEWPORT viewports[SLANG_COUNT_OF(state.viewports)];
        for (uint32_t i = 0; i < state.viewportCount; ++i)
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
        m_cmdList->RSSetViewports(state.viewportCount, viewports);
    }

    if (updateScissorRects)
    {
        static const uint32_t kMaxScissorRects = D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        SLANG_RHI_ASSERT(state.scissorRectCount <= kMaxScissorRects);
        D3D12_RECT scissorRects[SLANG_COUNT_OF(state.scissorRects)];
        for (uint32_t i = 0; i < state.scissorRectCount; ++i)
        {
            const ScissorRect& src = state.scissorRects[i];
            D3D12_RECT& dst = scissorRects[i];
            dst.left = LONG(src.minX);
            dst.top = LONG(src.minY);
            dst.right = LONG(src.maxX);
            dst.bottom = LONG(src.maxY);
        }
        m_cmdList->RSSetScissorRects(state.scissorRectCount, scissorRects);
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

void CommandRecorder::cmdDraw(const commands::Draw& cmd)
{
    if (!m_renderStateValid)
        return;

    m_cmdList->DrawInstanced(
        cmd.args.vertexCount,
        cmd.args.instanceCount,
        cmd.args.startIndexLocation,
        cmd.args.startInstanceLocation
    );
}

void CommandRecorder::cmdDrawIndexed(const commands::DrawIndexed& cmd)
{
    if (!m_renderStateValid)
        return;

    m_cmdList->DrawIndexedInstanced(
        cmd.args.vertexCount,
        cmd.args.instanceCount,
        cmd.args.startIndexLocation,
        cmd.args.startVertexLocation,
        cmd.args.startInstanceLocation
    );
}

void CommandRecorder::cmdDrawIndirect(const commands::DrawIndirect& cmd)
{
    if (!m_renderStateValid)
        return;

    BufferImpl* argBuffer = checked_cast<BufferImpl*>(cmd.argBuffer);
    BufferImpl* countBuffer = checked_cast<BufferImpl*>(cmd.countBuffer);

    requireBufferState(argBuffer, ResourceState::IndirectArgument);
    if (countBuffer)
    {
        requireBufferState(countBuffer, ResourceState::IndirectArgument);
    }

    m_cmdList->ExecuteIndirect(
        m_device->drawIndirectCmdSignature,
        cmd.maxDrawCount,
        argBuffer->m_resource,
        cmd.argOffset,
        countBuffer ? countBuffer->m_resource.getResource() : nullptr,
        cmd.countOffset
    );
}

void CommandRecorder::cmdDrawIndexedIndirect(const commands::DrawIndexedIndirect& cmd)
{
    if (!m_renderStateValid)
        return;

    BufferImpl* argBuffer = checked_cast<BufferImpl*>(cmd.argBuffer);
    BufferImpl* countBuffer = checked_cast<BufferImpl*>(cmd.countBuffer);

    requireBufferState(argBuffer, ResourceState::IndirectArgument);
    if (countBuffer)
    {
        requireBufferState(countBuffer, ResourceState::IndirectArgument);
    }

    m_cmdList->ExecuteIndirect(
        m_device->drawIndexedIndirectCmdSignature,
        cmd.maxDrawCount,
        argBuffer->m_resource,
        cmd.argOffset,
        countBuffer ? countBuffer->m_resource.getResource() : nullptr,
        cmd.countOffset
    );
}

void CommandRecorder::cmdDrawMeshTasks(const commands::DrawMeshTasks& cmd)
{
    if (!m_renderStateValid)
        return;

    m_cmdList6->DispatchMesh(cmd.x, cmd.y, cmd.z);
}

void CommandRecorder::cmdBeginComputePass(const commands::BeginComputePass& cmd)
{
    m_computePassActive = true;
}

void CommandRecorder::cmdEndComputePass(const commands::EndComputePass& cmd)
{
    m_computePassActive = false;
}

void CommandRecorder::cmdSetComputeState(const commands::SetComputeState& cmd)
{
    if (!m_computePassActive)
        return;

    const ComputeState& state = cmd.state;

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
        auto rootObject = checked_cast<RootShaderObjectImpl*>(state.rootObject);
        ComputeSubmitter submitter(m_device->m_device, m_cmdList);
        if (bindRootObject(rootObject, m_computePipeline->m_rootObjectLayout, &submitter) != SLANG_OK)
        {
            // TODO issue a message?
            return;
        }
    }

    m_computeStateValid = true;
    m_computeState = state;
}

void CommandRecorder::cmdDispatchCompute(const commands::DispatchCompute& cmd)
{
    if (!m_computeStateValid)
        return;

    m_cmdList->Dispatch(cmd.x, cmd.y, cmd.z);
}

void CommandRecorder::cmdDispatchComputeIndirect(const commands::DispatchComputeIndirect& cmd)
{
    if (!m_computeStateValid)
        return;

    BufferImpl* argBuffer = checked_cast<BufferImpl*>(cmd.argBuffer);

    requireBufferState(argBuffer, ResourceState::IndirectArgument);
    commitBarriers();

    m_cmdList->ExecuteIndirect(
        m_device->dispatchIndirectCmdSignature,
        (UINT)1,
        argBuffer->m_resource,
        (UINT64)cmd.offset,
        nullptr,
        0
    );
}

void CommandRecorder::cmdBeginRayTracingPass(const commands::BeginRayTracingPass& cmd)
{
    m_rayTracingPassActive = true;
}

void CommandRecorder::cmdEndRayTracingPass(const commands::EndRayTracingPass& cmd)
{
    m_rayTracingPassActive = false;
}

void CommandRecorder::cmdSetRayTracingState(const commands::SetRayTracingState& cmd)
{
    if (!m_rayTracingPassActive)
        return;

    const RayTracingState& state = cmd.state;

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
        auto rootObject = checked_cast<RootShaderObjectImpl*>(state.rootObject);
        ComputeSubmitter submitter(m_device->m_device, m_cmdList);
        if (bindRootObject(rootObject, m_rayTracingPipeline->m_rootObjectLayout, &submitter) != SLANG_OK)
        {
            // TODO issue a message?
            return;
        }
    }

    if (updateShaderTable)
    {
        m_shaderTable = checked_cast<ShaderTableImpl*>(state.shaderTable);

        Buffer* shaderTableBuffer = m_shaderTable->getOrCreateBuffer(m_rayTracingPipeline);
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

void CommandRecorder::cmdDispatchRays(const commands::DispatchRays& cmd)
{
    if (!m_rayTracingStateValid)
        return;

    m_dispatchRaysDesc.RayGenerationShaderRecord.StartAddress =
        m_rayGenTableAddr + cmd.rayGenShaderIndex * kRayGenRecordSize;
    m_dispatchRaysDesc.Width = cmd.width;
    m_dispatchRaysDesc.Height = cmd.height;
    m_dispatchRaysDesc.Depth = cmd.depth;
    m_cmdList4->DispatchRays(&m_dispatchRaysDesc);
}

void CommandRecorder::cmdBuildAccelerationStructure(const commands::BuildAccelerationStructure& cmd)
{
    AccelerationStructureImpl* dst = checked_cast<AccelerationStructureImpl*>(cmd.dst);
    AccelerationStructureImpl* src = checked_cast<AccelerationStructureImpl*>(cmd.src);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.DestAccelerationStructureData = dst->getDeviceAddress();
    buildDesc.SourceAccelerationStructureData = src ? src->getDeviceAddress() : 0;
    buildDesc.ScratchAccelerationStructureData = cmd.scratchBuffer.getDeviceAddress();
    AccelerationStructureInputsBuilder builder;
    builder.build(cmd.desc, m_device->m_debugCallback);
    buildDesc.Inputs = builder.desc;

    std::vector<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC> postBuildInfoDescs;
    translatePostBuildInfoDescs(cmd.propertyQueryCount, cmd.queryDescs, postBuildInfoDescs);
    m_cmdList4
        ->BuildRaytracingAccelerationStructure(&buildDesc, cmd.propertyQueryCount, postBuildInfoDescs.data());
}

void CommandRecorder::cmdCopyAccelerationStructure(const commands::CopyAccelerationStructure& cmd)
{
    auto dst = checked_cast<AccelerationStructureImpl*>(cmd.dst);
    auto src = checked_cast<AccelerationStructureImpl*>(cmd.src);
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE copyMode;
    switch (cmd.mode)
    {
    case AccelerationStructureCopyMode::Clone:
        copyMode = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_CLONE;
        break;
    case AccelerationStructureCopyMode::Compact:
        copyMode = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT;
        break;
    default:
        return;
    }
    m_cmdList4->CopyRaytracingAccelerationStructure(dst->getDeviceAddress(), src->getDeviceAddress(), copyMode);
}

void CommandRecorder::cmdQueryAccelerationStructureProperties(const commands::QueryAccelerationStructureProperties& cmd)
{
    std::vector<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC> postBuildInfoDescs;
    std::vector<DeviceAddress> asAddresses;
    asAddresses.resize(cmd.accelerationStructureCount);
    for (GfxIndex i = 0; i < cmd.accelerationStructureCount; i++)
        asAddresses[i] = cmd.accelerationStructures[i]->getDeviceAddress();
    translatePostBuildInfoDescs(cmd.queryCount, cmd.queryDescs, postBuildInfoDescs);
    m_cmdList4->EmitRaytracingAccelerationStructurePostbuildInfo(
        postBuildInfoDescs.data(),
        (UINT)cmd.accelerationStructureCount,
        asAddresses.data()
    );
}

void CommandRecorder::cmdSerializeAccelerationStructure(const commands::SerializeAccelerationStructure& cmd)
{
    auto src = checked_cast<AccelerationStructureImpl*>(cmd.src);
    m_cmdList4->CopyRaytracingAccelerationStructure(
        cmd.dst.getDeviceAddress(),
        src->getDeviceAddress(),
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_SERIALIZE
    );
}

void CommandRecorder::cmdDeserializeAccelerationStructure(const commands::DeserializeAccelerationStructure& cmd)
{
    auto dst = checked_cast<AccelerationStructureImpl*>(cmd.dst);
    m_cmdList4->CopyRaytracingAccelerationStructure(
        dst->getDeviceAddress(),
        cmd.src.getDeviceAddress(),
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_DESERIALIZE
    );
}

void CommandRecorder::cmdSetBufferState(const commands::SetBufferState& cmd)
{
    m_stateTracking.setBufferState(checked_cast<BufferImpl*>(cmd.buffer), cmd.state);
}

void CommandRecorder::cmdSetTextureState(const commands::SetTextureState& cmd)
{
    m_stateTracking.setTextureState(checked_cast<TextureImpl*>(cmd.texture), cmd.subresourceRange, cmd.state);
}

void CommandRecorder::cmdPushDebugGroup(const commands::PushDebugGroup& cmd)
{
    auto beginEvent = m_device->m_BeginEventOnCommandList;
    if (beginEvent)
    {
        UINT64 color = 0xff000000;
        color |= uint8_t(cmd.rgbColor[0] * 255.0f) << 16;
        color |= uint8_t(cmd.rgbColor[1] * 255.0f) << 8;
        color |= uint8_t(cmd.rgbColor[2] * 255.0f);
        beginEvent(m_cmdList, color, cmd.name);
    }
}

void CommandRecorder::cmdPopDebugGroup(const commands::PopDebugGroup& cmd)
{
    auto endEvent = m_device->m_EndEventOnCommandList;
    if (endEvent)
    {
        endEvent(m_cmdList);
    }
}

void CommandRecorder::cmdInsertDebugMarker(const commands::InsertDebugMarker& cmd)
{
    auto setMarker = m_device->m_SetMarkerOnCommandList;
    if (setMarker)
    {
        UINT64 color = 0xff000000;
        color |= uint8_t(cmd.rgbColor[0] * 255.0f) << 16;
        color |= uint8_t(cmd.rgbColor[1] * 255.0f) << 8;
        color |= uint8_t(cmd.rgbColor[2] * 255.0f);
        setMarker(m_cmdList, color, cmd.name);
    }
}

void CommandRecorder::cmdWriteTimestamp(const commands::WriteTimestamp& cmd)
{
    auto queryPool = checked_cast<QueryPoolImpl*>(cmd.queryPool);
    queryPool->writeTimestamp(m_cmdList, cmd.queryIndex);
}

void CommandRecorder::cmdExecuteCallback(const commands::ExecuteCallback& cmd)
{
    cmd.callback(cmd.userData);
}

struct BindingContextImpl : public BindingContext
{
    CommandRecorder* recorder;

    virtual Result allocateConstantBuffer(size_t size, BufferImpl*& outBufferWeakPtr, size_t& outOffset) override
    {
        auto allocation = recorder->m_constantBufferPool->allocate(size);
        outBufferWeakPtr = allocation.resource;
        outOffset = allocation.offset;
        return SLANG_OK;
    }

    virtual Result writeBuffer(BufferImpl* buffer, size_t offset, size_t size, const void* data) override
    {
        auto allocation = recorder->m_uploadBufferPool->allocate(size);
        ID3D12Resource* stagingBuffer = allocation.resource->m_resource.getResource();
        D3D12_RANGE readRange = {};
        readRange.Begin = 0;
        readRange.End = 0;
        void* uploadData;
        SLANG_RETURN_ON_FAIL(stagingBuffer->Map(0, &readRange, reinterpret_cast<void**>(&uploadData)));
        memcpy((uint8_t*)uploadData + allocation.offset, data, size);
        D3D12_RANGE writtenRange = {};
        writtenRange.Begin = allocation.offset;
        writtenRange.End = allocation.offset + size;
        stagingBuffer->Unmap(0, &writtenRange);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.pResource = buffer->m_resource.getResource();
        recorder->m_cmdList->ResourceBarrier(1, &barrier);

        recorder->m_cmdList
            ->CopyBufferRegion(buffer->m_resource.getResource(), offset, stagingBuffer, allocation.offset, size);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        recorder->m_cmdList->ResourceBarrier(1, &barrier);

        return SLANG_OK;
    }
};

Result CommandRecorder::bindRootObject(
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
    BindingContextImpl context = {};
    context.recorder = this;
    context.submitter = submitter;
    context.device = m_device;
    context.viewHeap = m_viewDescriptorHeap;
    context.samplerHeap = m_samplerDescriptorHeap;
    context.outOfMemoryHeap = (D3D12_DESCRIPTOR_HEAP_TYPE)(-1);

    // Transition all resources to the appropriate state and commit the barriers.
    // This needs to happen before binding descriptor tables, otherwise D3D12
    // will report validation errors.
    rootObject->setResourceStates(m_stateTracking);
    commitBarriers();

    // We kick off binding of shader objects at the root object, and the objects
    // themselves will be responsible for allocating, binding, and filling in
    // any descriptor tables or other root parameters needed.
    //
    bindDescriptorHeaps();
    SLANG_RETURN_ON_FAIL(rootObject->bindAsRoot(context, rootObjectLayout));
#if 0
    if (rootObject->bindAsRoot(context, rootObjectLayout) == SLANG_E_OUT_OF_MEMORY)
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
        SLANG_RETURN_ON_FAIL(rootObject->bindAsRoot(context, rootObjectLayout));
    }
#endif
    return SLANG_OK;
}

void CommandRecorder::bindDescriptorHeaps()
{
    if (!m_descriptorHeapsBound)
    {
        ID3D12DescriptorHeap* heaps[] = {
            m_viewDescriptorHeap->getHeap(),
            m_samplerDescriptorHeap->getHeap(),
        };
        m_cmdList->SetDescriptorHeaps(SLANG_COUNT_OF(heaps), heaps);
        m_descriptorHeapsBound = true;
    }
}

void CommandRecorder::requireBufferState(BufferImpl* buffer, ResourceState state)
{
    m_stateTracking.setBufferState(buffer, state);
}

void CommandRecorder::requireTextureState(TextureImpl* texture, SubresourceRange subresourceRange, ResourceState state)
{
    m_stateTracking.setTextureState(texture, subresourceRange, state);
}

void CommandRecorder::commitBarriers()
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

// CommandQueueImpl

CommandQueueImpl::CommandQueueImpl(DeviceImpl* device, QueueType type)
    : CommandQueue(device, type)
{
}

CommandQueueImpl::~CommandQueueImpl()
{
    waitOnHost();
    CloseHandle(m_globalWaitHandle);
}

Result CommandQueueImpl::init(uint32_t queueIndex)
{
    m_queueIndex = queueIndex;
    m_d3dDevice = m_device->m_device;
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    SLANG_RETURN_ON_FAIL(m_d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(m_d3dQueue.writeRef())));
    SLANG_RETURN_ON_FAIL(m_d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_trackingFence.writeRef())));
    m_globalWaitHandle =
        CreateEventEx(nullptr, nullptr, CREATE_EVENT_INITIAL_SET | CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
    return SLANG_OK;
}

Result CommandQueueImpl::createCommandBuffer(CommandBufferImpl** outCommandBuffer)
{
    RefPtr<CommandBufferImpl> commandBuffer = new CommandBufferImpl(m_device, this);
    SLANG_RETURN_ON_FAIL(commandBuffer->init());
    returnRefPtr(outCommandBuffer, commandBuffer);
    return SLANG_OK;
}

Result CommandQueueImpl::getOrCreateCommandBuffer(CommandBufferImpl** outCommandBuffer)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    RefPtr<CommandBufferImpl> commandBuffer;
    if (m_commandBuffersPool.empty())
    {
        SLANG_RETURN_ON_FAIL(createCommandBuffer(commandBuffer.writeRef()));
    }
    else
    {
        commandBuffer = m_commandBuffersPool.front();
        m_commandBuffersPool.pop_front();
    }
    returnRefPtr(outCommandBuffer, commandBuffer);
    return SLANG_OK;
}

void CommandQueueImpl::retireUnfinishedCommandBuffer(CommandBufferImpl* commandBuffer)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    commandBuffer->reset();
    m_commandBuffersPool.push_back(commandBuffer);
}

void CommandQueueImpl::retireCommandBuffers()
{
    std::list<RefPtr<CommandBufferImpl>> commandBuffers = std::move(m_commandBuffersInFlight);
    m_commandBuffersInFlight.clear();

    uint64_t lastFinishedID = updateLastFinishedID();
    for (const auto& commandBuffer : commandBuffers)
    {
        if (commandBuffer->m_submissionID <= lastFinishedID)
        {
            commandBuffer->reset();
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_commandBuffersPool.push_back(commandBuffer);
            }
        }
        else
        {
            m_commandBuffersInFlight.push_back(commandBuffer);
        }
    }
}

uint64_t CommandQueueImpl::updateLastFinishedID()
{
    m_lastFinishedID = m_trackingFence->GetCompletedValue();
    return m_lastFinishedID;
}

Result CommandQueueImpl::createCommandEncoder(ICommandEncoder** outEncoder)
{
    RefPtr<CommandEncoderImpl> encoder = new CommandEncoderImpl(m_device, this);
    SLANG_RETURN_ON_FAIL(encoder->init());
    returnComPtr(outEncoder, encoder);
    return SLANG_OK;
}

Result CommandQueueImpl::submit(
    GfxCount count,
    ICommandBuffer* const* commandBuffers,
    IFence* fence,
    uint64_t valueToSignal
)
{
    // Increment last submitted ID which is used to track command buffer completion.
    ++m_lastSubmittedID;

    short_vector<ID3D12CommandList*> commandLists;
    for (GfxCount i = 0; i < count; i++)
    {
        auto commandBuffer = checked_cast<CommandBufferImpl*>(commandBuffers[i]);
        commandBuffer->m_submissionID = m_lastSubmittedID;
        m_commandBuffersInFlight.push_back(commandBuffer);
        commandLists.push_back(commandBuffer->m_d3dCommandList);
    }
    if (count > 0)
    {
        m_d3dQueue->ExecuteCommandLists((UINT)count, commandLists.data());
    }

    if (fence)
    {
        auto fenceImpl = checked_cast<FenceImpl*>(fence);
        SLANG_RETURN_ON_FAIL(m_d3dQueue->Signal(fenceImpl->m_fence.get(), valueToSignal));
    }

    SLANG_RETURN_ON_FAIL(m_d3dQueue->Signal(m_trackingFence.get(), m_lastSubmittedID));

    retireCommandBuffers();

    return SLANG_OK;
}

Result CommandQueueImpl::waitOnHost()
{
    m_lastSubmittedID++;
    m_d3dQueue->Signal(m_trackingFence.get(), m_lastSubmittedID);
    ResetEvent(m_globalWaitHandle);
    m_trackingFence->SetEventOnCompletion(m_lastSubmittedID, m_globalWaitHandle);
    WaitForSingleObject(m_globalWaitHandle, INFINITE);
    m_device->flushValidationMessages();
    retireCommandBuffers();
    return SLANG_OK;
}

Result CommandQueueImpl::waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues)
{
    for (GfxCount i = 0; i < fenceCount; ++i)
    {
        auto fenceImpl = checked_cast<FenceImpl*>(fences[i]);
        m_d3dQueue->Wait(fenceImpl->m_fence.get(), waitValues[i]);
    }
    return SLANG_OK;
}

Result CommandQueueImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12CommandQueue;
    outHandle->value = (uint64_t)m_d3dQueue.get();
    return SLANG_OK;
}

// CommandEncoderImpl

// CommandEncoderImpl

CommandEncoderImpl::CommandEncoderImpl(DeviceImpl* device, CommandQueueImpl* queue)
    : m_device(device)
    , m_queue(queue)
{
}

CommandEncoderImpl::~CommandEncoderImpl()
{
    // If the command buffer was not used, return it to the pool.
    if (m_commandBuffer)
    {
        m_queue->retireUnfinishedCommandBuffer(m_commandBuffer);
    }
}

Result CommandEncoderImpl::init()
{
    SLANG_RETURN_ON_FAIL(m_queue->getOrCreateCommandBuffer(m_commandBuffer.writeRef()));
    m_commandList = m_commandBuffer->m_commandList;
    return SLANG_OK;
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
    // TODO: we should upload to the staging buffer here and only encode the copy command in the command buffer
    CommandEncoder::uploadTextureData(dst, subresourceRange, offset, extent, subresourceData, subresourceDataCount);
}

void CommandEncoderImpl::uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data)
{
    // TODO: we should upload to the staging buffer here and only encode the copy command in the command buffer
    CommandEncoder::uploadBufferData(dst, offset, size, data);
}

Result CommandEncoderImpl::finish(ICommandBuffer** outCommandBuffer)
{
    SLANG_RETURN_ON_FAIL(resolvePipelines(m_device));
    CommandRecorder recorder(m_device);
    SLANG_RETURN_ON_FAIL(recorder.record(m_commandBuffer));
    returnComPtr(outCommandBuffer, m_commandBuffer);
    m_commandBuffer = nullptr;
    m_commandList = nullptr;
    return SLANG_OK;
}

Result CommandEncoderImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

// CommandBufferImpl

CommandBufferImpl::CommandBufferImpl(DeviceImpl* device, CommandQueueImpl* queue)
    : m_device(device)
    , m_queue(queue)
{
}

CommandBufferImpl::~CommandBufferImpl() {}

Result CommandBufferImpl::init()
{
    m_commandList = new CommandList();

    SLANG_RETURN_ON_FAIL(m_device->m_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(m_d3dCommandAllocator.writeRef())
    ));
    SLANG_RETURN_ON_FAIL(m_device->m_device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_d3dCommandAllocator,
        nullptr,
        IID_PPV_ARGS(m_d3dCommandList.writeRef())
    ));

    SLANG_RETURN_ON_FAIL(m_viewDescriptorHeap.init(
        m_device->m_device,
        16 * 1024,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
    ));
    SLANG_RETURN_ON_FAIL(m_samplerDescriptorHeap.init(
        m_device->m_device,
        1024,
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
    ));

    m_constantBufferPool
        .init(m_device, MemoryType::DeviceLocal, 256, BufferUsage::ConstantBuffer | BufferUsage::CopyDestination);
    m_uploadBufferPool.init(m_device, MemoryType::Upload, 256, BufferUsage::CopySource);

    return SLANG_OK;
}

Result CommandBufferImpl::reset()
{
    m_commandList->reset();
    SLANG_RETURN_ON_FAIL(m_d3dCommandAllocator->Reset());
    SLANG_RETURN_ON_FAIL(m_d3dCommandList->Reset(m_d3dCommandAllocator, nullptr));
    m_viewDescriptorHeap.deallocateAll();
    m_samplerDescriptorHeap.deallocateAll();
    m_constantBufferPool.reset();
    m_uploadBufferPool.reset();
    return SLANG_OK;
}

Result CommandBufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12GraphicsCommandList;
    outHandle->value = (uint64_t)m_d3dCommandList.get();
    return SLANG_OK;
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

} // namespace rhi::d3d12
