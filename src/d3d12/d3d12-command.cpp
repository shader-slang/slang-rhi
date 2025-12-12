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
#include "d3d12-utils.h"
#include "../state-tracking.h"
#include "../strings.h"
#include "../format-conversion.h"

#include "core/short_vector.h"
#include "core/common.h"

namespace rhi::d3d12 {

template<typename T>
inline bool arraysEqual(uint32_t countA, uint32_t countB, const T* a, const T* b)
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

    GPUDescriptorArena* m_cbvSrvUavArena = nullptr;
    GPUDescriptorArena* m_samplerArena = nullptr;

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
    RefPtr<ComputePipelineImpl> m_computePipeline;

    bool m_rayTracingPassActive = false;
    bool m_rayTracingStateValid = false;
    RefPtr<RayTracingPipelineImpl> m_rayTracingPipeline;
    RefPtr<ShaderTableImpl> m_shaderTable;
    D3D12_DISPATCH_RAYS_DESC m_dispatchRaysDesc = {};
    UINT64 m_rayGenTableAddr = 0;

    BindingDataImpl* m_bindingData = nullptr;

    CommandRecorder(DeviceImpl* device)
        : m_device(device)
    {
    }

    Result record(CommandBufferImpl* commandBuffer);

    void cmdCopyBuffer(const commands::CopyBuffer& cmd);
    void cmdCopyTexture(const commands::CopyTexture& cmd);
    void cmdCopyTextureToBuffer(const commands::CopyTextureToBuffer& cmd);
    void cmdClearBuffer(const commands::ClearBuffer& cmd);
    void cmdClearTextureFloat(const commands::ClearTextureFloat& cmd);
    void cmdClearTextureUint(const commands::ClearTextureUint& cmd);
    void cmdClearTextureDepthStencil(const commands::ClearTextureDepthStencil& cmd);
    void cmdUploadTextureData(const commands::UploadTextureData& cmd);
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
    void cmdExecuteClusterOperation(const commands::ExecuteClusterOperation& cmd);
    void cmdConvertCooperativeVectorMatrix(const commands::ConvertCooperativeVectorMatrix& cmd);
    void cmdSetBufferState(const commands::SetBufferState& cmd);
    void cmdSetTextureState(const commands::SetTextureState& cmd);
    void cmdGlobalBarrier(const commands::GlobalBarrier& cmd);
    void cmdPushDebugGroup(const commands::PushDebugGroup& cmd);
    void cmdPopDebugGroup(const commands::PopDebugGroup& cmd);
    void cmdInsertDebugMarker(const commands::InsertDebugMarker& cmd);
    void cmdWriteTimestamp(const commands::WriteTimestamp& cmd);
    void cmdExecuteCallback(const commands::ExecuteCallback& cmd);

    enum class BindMode
    {
        Graphics,
        Compute,
        RayTracing,
    };

    void setBindings(BindingDataImpl* bindingData, BindMode bindMode);

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
    m_cbvSrvUavArena = &commandBuffer->m_cbvSrvUavArena;
    m_samplerArena = &commandBuffer->m_samplerArena;

    CommandList& commandList = commandBuffer->m_commandList;

    for (const CommandList::CommandSlot* slot = commandList.getCommands(); slot; slot = slot->next)
    {
#define SLANG_RHI_COMMAND_EXECUTE_X(x)                                                                                 \
    case CommandID::x:                                                                                                 \
        cmd##x(commandList.getCommand<commands::x>(slot));                                                             \
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

#define NOT_SUPPORTED(x) m_device->printWarning(S_CommandEncoder_##x " command is not supported!")

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

    SubresourceRange dstSubresource = cmd.dstSubresource;
    const Offset3D& dstOffset = cmd.dstOffset;
    SubresourceRange srcSubresource = cmd.srcSubresource;
    const Offset3D& srcOffset = cmd.srcOffset;
    const Extent3D& extent = cmd.extent;

    // Fast path for copying whole resource.
    if (dstSubresource.layerCount == 0 && dstSubresource.mipCount == 0 && srcSubresource.layerCount == 0 &&
        srcSubresource.mipCount == 0 && srcOffset.isZero() && dstOffset.isZero() && extent.isWholeTexture())
    {
        requireTextureState(dst, kEntireTexture, ResourceState::CopyDestination);
        requireTextureState(src, kEntireTexture, ResourceState::CopySource);
        commitBarriers();
        m_cmdList->CopyResource(dst->m_resource.getResource(), src->m_resource.getResource());
        return;
    }

    // If we couldn't use the fast CopyResource path, need to ensure that the subresource ranges are valid.
    if (dstSubresource.layerCount == 0)
        dstSubresource.layerCount = dst->m_desc.getLayerCount();
    if (dstSubresource.mipCount == 0)
        dstSubresource.mipCount = dst->m_desc.mipCount;
    if (srcSubresource.layerCount == 0)
        srcSubresource.layerCount = src->m_desc.getLayerCount();
    if (srcSubresource.mipCount == 0)
        srcSubresource.mipCount = src->m_desc.mipCount;

    // Validate subresource ranges
    SLANG_RHI_ASSERT(srcSubresource.layer + srcSubresource.layerCount <= src->m_desc.getLayerCount());
    SLANG_RHI_ASSERT(dstSubresource.layer + dstSubresource.layerCount <= dst->m_desc.getLayerCount());
    SLANG_RHI_ASSERT(srcSubresource.mip + srcSubresource.mipCount <= src->m_desc.mipCount);
    SLANG_RHI_ASSERT(dstSubresource.mip + dstSubresource.mipCount <= dst->m_desc.mipCount);

    // Validate matching dimensions between source and destination
    SLANG_RHI_ASSERT(srcSubresource.layerCount == dstSubresource.layerCount);
    SLANG_RHI_ASSERT(srcSubresource.mipCount == dstSubresource.mipCount);

    requireTextureState(dst, dstSubresource, ResourceState::CopyDestination);
    requireTextureState(src, srcSubresource, ResourceState::CopySource);
    commitBarriers();

    Extent3D srcTextureSize = src->m_desc.size;

    uint32_t planeCount = getPlaneSliceCount(dst->m_format);
    SLANG_RHI_ASSERT(planeCount == getPlaneSliceCount(src->m_format));
    SLANG_RHI_ASSERT(planeCount > 0);

    for (uint32_t planeIndex = 0; planeIndex < planeCount; planeIndex++)
    {
        for (uint32_t layer = 0; layer < dstSubresource.layerCount; layer++)
        {
            for (uint32_t mipOffset = 0; mipOffset < dstSubresource.mipCount; mipOffset++)
            {
                uint32_t srcMip = srcSubresource.mip + mipOffset;
                uint32_t dstMip = dstSubresource.mip + mipOffset;

                // Calculate adjusted extents. Note it is required and enforced
                // by debug layer that if 'remaining texture' is used, src and
                // dst offsets are the same.
                Extent3D srcMipSize = calcMipSize(srcTextureSize, srcMip);
                Extent3D adjustedExtent = extent;
                if (adjustedExtent.width == kRemainingTextureSize)
                {
                    SLANG_RHI_ASSERT(srcOffset.x == dstOffset.x);
                    adjustedExtent.width = srcMipSize.width - srcOffset.x;
                }
                if (adjustedExtent.height == kRemainingTextureSize)
                {
                    SLANG_RHI_ASSERT(srcOffset.y == dstOffset.y);
                    adjustedExtent.height = srcMipSize.height - srcOffset.y;
                }
                if (adjustedExtent.depth == kRemainingTextureSize)
                {
                    SLANG_RHI_ASSERT(srcOffset.z == dstOffset.z);
                    adjustedExtent.depth = srcMipSize.depth - srcOffset.z;
                }

                // Validate source and destination parameters
                SLANG_RHI_ASSERT(srcOffset.x + adjustedExtent.width <= srcMipSize.width);
                SLANG_RHI_ASSERT(srcOffset.y + adjustedExtent.height <= srcMipSize.height);
                SLANG_RHI_ASSERT(srcOffset.z + adjustedExtent.depth <= srcMipSize.depth);
                SLANG_RHI_ASSERT(srcMip < src->m_desc.mipCount);

                D3D12_TEXTURE_COPY_LOCATION dstRegion = {};

                dstRegion.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dstRegion.pResource = dst->m_resource.getResource();
                dstRegion.SubresourceIndex = getSubresourceIndex(
                    dstMip,
                    dstSubresource.layer + layer,
                    planeIndex,
                    dst->m_desc.mipCount,
                    dst->m_desc.getLayerCount()
                );

                D3D12_TEXTURE_COPY_LOCATION srcRegion = {};
                srcRegion.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                srcRegion.pResource = src->m_resource.getResource();
                srcRegion.SubresourceIndex = getSubresourceIndex(
                    srcMip,
                    srcSubresource.layer + layer,
                    planeIndex,
                    src->m_desc.mipCount,
                    src->m_desc.getLayerCount()
                );

                if (srcOffset.isZero() && dstOffset.isZero() && adjustedExtent == srcMipSize)
                {
                    // If copying whole texture region, pass nullptr. This is required for
                    // copying certain resources such as depth-stencil or multisampled textures.
                    m_cmdList->CopyTextureRegion(&dstRegion, 0, 0, 0, &srcRegion, nullptr);
                }
                else
                {
                    D3D12_BOX srcBox = {};
                    srcBox.left = srcOffset.x;
                    srcBox.top = srcOffset.y;
                    srcBox.front = srcOffset.z;
                    srcBox.right = srcBox.left + adjustedExtent.width;
                    srcBox.bottom = srcBox.top + adjustedExtent.height;
                    srcBox.back = srcBox.front + adjustedExtent.depth;

                    m_cmdList
                        ->CopyTextureRegion(&dstRegion, dstOffset.x, dstOffset.y, dstOffset.z, &srcRegion, &srcBox);
                }
            }
        }
    }
}

void CommandRecorder::cmdCopyTextureToBuffer(const commands::CopyTextureToBuffer& cmd)
{
    BufferImpl* dst = checked_cast<BufferImpl*>(cmd.dst);
    TextureImpl* src = checked_cast<TextureImpl*>(cmd.src);

    const TextureDesc& srcDesc = src->getDesc();
    Extent3D textureSize = srcDesc.size;
    const FormatInfo& formatInfo = getFormatInfo(srcDesc.format);

    const uint64_t dstOffset = cmd.dstOffset;
    const Size dstRowPitch = cmd.dstRowPitch;
    uint32_t srcLayer = cmd.srcLayer;
    uint32_t srcMip = cmd.srcMip;
    const Offset3D& srcOffset = cmd.srcOffset;
    const Extent3D& extent = cmd.extent;

    // Switch texture to copy src and buffer to copy dest.
    requireBufferState(dst, ResourceState::CopyDestination);
    requireTextureState(src, {srcLayer, 1, srcMip, 1}, ResourceState::CopySource);
    commitBarriers();

    // Calculate adjusted extents. Note it is required and enforced
    // by debug layer that if 'remaining texture' is used, src and
    // dst offsets are the same.
    Extent3D srcMipSize = calcMipSize(textureSize, srcMip);
    Extent3D adjustedExtent = extent;
    if (adjustedExtent.width == kRemainingTextureSize)
    {
        SLANG_RHI_ASSERT(srcMipSize.width >= srcOffset.x);
        adjustedExtent.width = srcMipSize.width - srcOffset.x;
    }
    if (adjustedExtent.height == kRemainingTextureSize)
    {
        SLANG_RHI_ASSERT(srcMipSize.height >= srcOffset.y);
        adjustedExtent.height = srcMipSize.height - srcOffset.y;
    }
    if (adjustedExtent.depth == kRemainingTextureSize)
    {
        SLANG_RHI_ASSERT(srcMipSize.depth >= srcOffset.z);
        adjustedExtent.depth = srcMipSize.depth - srcOffset.z;
    }

    // Align extents to block size
    adjustedExtent.width = math::calcAligned(adjustedExtent.width, formatInfo.blockWidth);
    adjustedExtent.height = math::calcAligned(adjustedExtent.height, formatInfo.blockHeight);

    // Setup the source resource.
    D3D12_TEXTURE_COPY_LOCATION srcRegion = {};
    srcRegion.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcRegion.SubresourceIndex =
        getSubresourceIndex(srcMip, srcLayer, 0, src->m_desc.mipCount, src->m_desc.arrayLength);
    srcRegion.pResource = src->m_resource.getResource();

    // Setup the destination resource.
    D3D12_TEXTURE_COPY_LOCATION dstRegion = {};
    dstRegion.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstRegion.pResource = dst->m_resource.getResource();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint = dstRegion.PlacedFootprint;
    footprint.Offset = dstOffset;
    footprint.Footprint.Format = src->m_resource.getResource()->GetDesc().Format;

    // Write adjusted extent to footprint, accounting for block size.
    footprint.Footprint.Width = adjustedExtent.width;
    footprint.Footprint.Height = adjustedExtent.height;
    footprint.Footprint.Depth = adjustedExtent.depth;

    // Align row pitch to 256 bytes
    SLANG_RHI_ASSERT(dstRowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT == 0);
    footprint.Footprint.RowPitch = (UINT)dstRowPitch;

    if (srcOffset.isZero() && adjustedExtent == srcMipSize)
    {
        // If copying whole texture region, pass nullptr. This is required for
        // copying certain resources such as depth-stencil or multisampled textures.
        m_cmdList->CopyTextureRegion(&dstRegion, 0, 0, 0, &srcRegion, nullptr);
    }
    else
    {
        // Not copying whole texture so pass offsets in
        D3D12_BOX srcBox = {};
        srcBox.left = srcOffset.x;
        srcBox.top = srcOffset.y;
        srcBox.front = srcOffset.z;
        srcBox.right = srcOffset.x + adjustedExtent.width;
        srcBox.bottom = srcOffset.y + adjustedExtent.height;
        srcBox.back = srcOffset.z + adjustedExtent.depth;
        m_cmdList->CopyTextureRegion(&dstRegion, 0, 0, 0, &srcRegion, &srcBox);
    }
}

void CommandRecorder::cmdClearBuffer(const commands::ClearBuffer& cmd)
{
    BufferImpl* buffer = checked_cast<BufferImpl*>(cmd.buffer);
    requireBufferState(buffer, ResourceState::UnorderedAccess);
    commitBarriers();

    D3D12_CPU_DESCRIPTOR_HANDLE uav = buffer->getUAV(Format::R32Uint, 0, cmd.range);
    GPUDescriptorRange descriptor = m_cbvSrvUavArena->allocate(1);
    m_device->m_device
        ->CopyDescriptorsSimple(1, descriptor.getCpuHandle(0), uav, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    UINT clearValues[4] = {0, 0, 0, 0};
    m_cmdList->ClearUnorderedAccessViewUint(
        descriptor.getGpuHandle(0),
        uav,
        buffer->m_resource.getResource(),
        clearValues,
        0,
        nullptr
    );
}

void CommandRecorder::cmdClearTextureFloat(const commands::ClearTextureFloat& cmd)
{
    TextureImpl* texture = checked_cast<TextureImpl*>(cmd.texture);
    const TextureDesc& desc = texture->m_desc;
    if (is_set(desc.usage, TextureUsage::RenderTarget))
    {
        requireTextureState(texture, cmd.subresourceRange, ResourceState::RenderTarget);
        commitBarriers();
        for (uint32_t mipOffset = 0; mipOffset < cmd.subresourceRange.mipCount; ++mipOffset)
        {
            SubresourceRange sr = cmd.subresourceRange;
            sr.mip = cmd.subresourceRange.mip + mipOffset;
            sr.mipCount = 1;
            D3D12_CPU_DESCRIPTOR_HANDLE rtv = texture->getRTV(desc.format, desc.type, TextureAspect::All, sr);
            m_cmdList->ClearRenderTargetView(rtv, cmd.clearValue, 0, nullptr);
        }
    }
    else if (is_set(desc.usage, TextureUsage::UnorderedAccess))
    {
        requireTextureState(texture, cmd.subresourceRange, ResourceState::UnorderedAccess);
        commitBarriers();
        for (uint32_t mipOffset = 0; mipOffset < cmd.subresourceRange.mipCount; ++mipOffset)
        {
            SubresourceRange sr = cmd.subresourceRange;
            sr.mip = cmd.subresourceRange.mip + mipOffset;
            sr.mipCount = 1;
            D3D12_CPU_DESCRIPTOR_HANDLE uav = texture->getUAV(desc.format, desc.type, TextureAspect::All, sr);
            GPUDescriptorRange descriptor = m_cbvSrvUavArena->allocate(1);
            m_device->m_device
                ->CopyDescriptorsSimple(1, descriptor.getCpuHandle(0), uav, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            m_cmdList->ClearUnorderedAccessViewFloat(
                descriptor.getGpuHandle(0),
                uav,
                texture->m_resource.getResource(),
                cmd.clearValue,
                0,
                nullptr
            );
        }
    }
}

void CommandRecorder::cmdClearTextureUint(const commands::ClearTextureUint& cmd)
{
    TextureImpl* texture = checked_cast<TextureImpl*>(cmd.texture);
    const TextureDesc& desc = texture->m_desc;
    if (is_set(desc.usage, TextureUsage::UnorderedAccess))
    {
        requireTextureState(texture, cmd.subresourceRange, ResourceState::UnorderedAccess);
        commitBarriers();
        for (uint32_t mipOffset = 0; mipOffset < cmd.subresourceRange.mipCount; ++mipOffset)
        {
            SubresourceRange sr = cmd.subresourceRange;
            sr.mip = cmd.subresourceRange.mip + mipOffset;
            sr.mipCount = 1;
            D3D12_CPU_DESCRIPTOR_HANDLE uav = texture->getUAV(desc.format, desc.type, TextureAspect::All, sr);
            GPUDescriptorRange descriptor = m_cbvSrvUavArena->allocate(1);
            m_device->m_device
                ->CopyDescriptorsSimple(1, descriptor.getCpuHandle(0), uav, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            uint32_t clearValue[4];
            truncateBySintFormat(desc.format, cmd.clearValue, clearValue);
            m_cmdList->ClearUnorderedAccessViewUint(
                descriptor.getGpuHandle(0),
                uav,
                texture->m_resource.getResource(),
                clearValue,
                0,
                nullptr
            );
        }
    }
}

void CommandRecorder::cmdClearTextureDepthStencil(const commands::ClearTextureDepthStencil& cmd)
{
    TextureImpl* texture = checked_cast<TextureImpl*>(cmd.texture);
    const TextureDesc& desc = texture->m_desc;
    if (is_set(desc.usage, TextureUsage::DepthStencil))
    {
        requireTextureState(texture, cmd.subresourceRange, ResourceState::DepthWrite);
        commitBarriers();
        D3D12_CLEAR_FLAGS clearFlags = (D3D12_CLEAR_FLAGS)0;
        if (cmd.clearDepth)
            clearFlags |= D3D12_CLEAR_FLAG_DEPTH;
        if (cmd.clearStencil)
            clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
        for (uint32_t mipOffset = 0; mipOffset < cmd.subresourceRange.mipCount; ++mipOffset)
        {
            SubresourceRange sr = cmd.subresourceRange;
            sr.mip = cmd.subresourceRange.mip + mipOffset;
            sr.mipCount = 1;
            D3D12_CPU_DESCRIPTOR_HANDLE dsv = texture->getDSV(desc.format, desc.type, TextureAspect::All, sr);
            m_cmdList->ClearDepthStencilView(dsv, clearFlags, cmd.depthValue, cmd.stencilValue, 0, nullptr);
        }
    }
}

void CommandRecorder::cmdUploadTextureData(const commands::UploadTextureData& cmd)
{
    auto buffer = checked_cast<BufferImpl*>(cmd.srcBuffer);
    auto dst = checked_cast<TextureImpl*>(cmd.dst);
    SubresourceRange subresourceRange = cmd.subresourceRange;

    requireBufferState(buffer, ResourceState::CopySource);
    requireTextureState(dst, subresourceRange, ResourceState::CopyDestination);
    commitBarriers();

    SubresourceLayout* srLayout = cmd.layouts;
    Offset bufferOffset = cmd.srcOffset;

    for (uint32_t layerOffset = 0; layerOffset < subresourceRange.layerCount; layerOffset++)
    {
        uint32_t layer = subresourceRange.layer + layerOffset;
        for (uint32_t mipOffset = 0; mipOffset < subresourceRange.mipCount; mipOffset++)
        {
            uint32_t mip = subresourceRange.mip + mipOffset;

            D3D12_RESOURCE_DESC texDesc = dst->m_resource.getResource()->GetDesc();

            D3D12_TEXTURE_COPY_LOCATION dstRegion = {};
            dstRegion.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dstRegion.pResource = dst->m_resource.getResource();

            dstRegion.SubresourceIndex =
                getSubresourceIndex(mip, layer, 0, dst->m_desc.mipCount, dst->m_desc.arrayLength);

            D3D12_TEXTURE_COPY_LOCATION srcRegion = {};
            srcRegion.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            srcRegion.pResource = buffer->m_resource.getResource();

            D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint = srcRegion.PlacedFootprint;
            footprint.Offset = bufferOffset;
            footprint.Footprint.Format = texDesc.Format;
            footprint.Footprint.Width = srLayout->size.width;
            footprint.Footprint.Height = srLayout->size.height;
            footprint.Footprint.Depth = srLayout->size.depth;
            footprint.Footprint.RowPitch = srLayout->rowPitch;

            m_cmdList->CopyTextureRegion(&dstRegion, cmd.offset.x, cmd.offset.y, cmd.offset.z, &srcRegion, nullptr);

            bufferOffset += srLayout->sizeInBytes;
            srLayout++;
        }
    }
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
            cmd.offset,
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
        renderTargetDescriptors.push_back(m_renderTargetViews[i]->getRTV());
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

    D3D12_CPU_DESCRIPTOR_HANDLE depthStencilDescriptor = {};
    if (m_depthStencilView)
        depthStencilDescriptor = m_depthStencilView->getDSV();

    m_cmdList->OMSetRenderTargets(
        (UINT)m_renderTargetViews.size(),
        renderTargetDescriptors.data(),
        FALSE,
        m_depthStencilView ? &depthStencilDescriptor : nullptr
    );

    // Issue clear commands based on render pass set up.
    for (size_t i = 0; i < m_renderTargetViews.size(); i++)
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
                m_depthStencilView->getDSV(),
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
                // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resolvesubresource
                DXGI_FORMAT format = srcView->m_texture->m_format;
                if (!dstView->m_texture->m_isTypeless)
                {
                    format = dstView->m_texture->m_format;
                }
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

    bool updatePipeline = !m_renderStateValid || cmd.pipeline != m_renderPipeline;
    bool updateBindings = updatePipeline || cmd.bindingData != m_bindingData;
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
        m_renderPipeline = checked_cast<RenderPipelineImpl*>(cmd.pipeline);
        m_cmdList->SetGraphicsRootSignature(m_renderPipeline->m_rootObjectLayout->m_rootSignature);
        m_cmdList->SetPipelineState(m_renderPipeline->m_pipelineState);
        m_cmdList->IASetPrimitiveTopology(m_renderPipeline->m_primitiveTopology);
    }

    if (updateBindings)
    {
        m_bindingData = static_cast<BindingDataImpl*>(cmd.bindingData);
        setBindings(m_bindingData, BindMode::Graphics);
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
            uint64_t offset = state.vertexBuffers[i].offset;
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
            uint64_t offset = state.indexBuffer.offset;
            requireBufferState(buffer, ResourceState::IndexBuffer);

            D3D12_INDEX_BUFFER_VIEW indexBufferView;
            indexBufferView.BufferLocation = buffer->m_resource.getResource()->GetGPUVirtualAddress() + offset;
            indexBufferView.SizeInBytes = UINT(buffer->m_desc.size - offset);
            indexBufferView.Format = getIndexFormat(state.indexFormat);
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
            dst.left = src.minX;
            dst.top = src.minY;
            dst.right = src.maxX;
            dst.bottom = src.maxY;
        }
        m_cmdList->RSSetScissorRects(state.scissorRectCount, scissorRects);
    }

    m_renderStateValid = true;
    m_renderState = state;

    m_computeStateValid = false;
    m_computePipeline = nullptr;

    m_rayTracingStateValid = false;
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

    BufferImpl* argBuffer = checked_cast<BufferImpl*>(cmd.argBuffer.buffer);
    BufferImpl* countBuffer = checked_cast<BufferImpl*>(cmd.countBuffer.buffer);

    requireBufferState(argBuffer, ResourceState::IndirectArgument);
    if (countBuffer)
    {
        requireBufferState(countBuffer, ResourceState::IndirectArgument);
    }

    m_cmdList->ExecuteIndirect(
        m_device->drawIndirectCmdSignature,
        cmd.maxDrawCount,
        argBuffer->m_resource,
        cmd.argBuffer.offset,
        countBuffer ? countBuffer->m_resource.getResource() : nullptr,
        cmd.countBuffer.offset
    );
}

void CommandRecorder::cmdDrawIndexedIndirect(const commands::DrawIndexedIndirect& cmd)
{
    if (!m_renderStateValid)
        return;

    BufferImpl* argBuffer = checked_cast<BufferImpl*>(cmd.argBuffer.buffer);
    BufferImpl* countBuffer = checked_cast<BufferImpl*>(cmd.countBuffer.buffer);

    requireBufferState(argBuffer, ResourceState::IndirectArgument);
    if (countBuffer)
    {
        requireBufferState(countBuffer, ResourceState::IndirectArgument);
    }

    m_cmdList->ExecuteIndirect(
        m_device->drawIndexedIndirectCmdSignature,
        cmd.maxDrawCount,
        argBuffer->m_resource,
        cmd.argBuffer.offset,
        countBuffer ? countBuffer->m_resource.getResource() : nullptr,
        cmd.countBuffer.offset
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

    bool updatePipeline = !m_computeStateValid || cmd.pipeline != m_computePipeline;
    bool updateBindings = updatePipeline || cmd.bindingData != m_bindingData;

    if (updatePipeline)
    {
        m_computePipeline = checked_cast<ComputePipelineImpl*>(cmd.pipeline);
        m_cmdList->SetComputeRootSignature(m_computePipeline->m_rootObjectLayout->m_rootSignature);
        m_cmdList->SetPipelineState(m_computePipeline->m_pipelineState);
    }

    if (updateBindings)
    {
        m_bindingData = static_cast<BindingDataImpl*>(cmd.bindingData);
        setBindings(m_bindingData, BindMode::Compute);
    }

    m_computeStateValid = true;

    m_renderStateValid = false;
    m_rayTracingStateValid = false;
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

    BufferImpl* argBuffer = checked_cast<BufferImpl*>(cmd.argBuffer.buffer);

    requireBufferState(argBuffer, ResourceState::IndirectArgument);
    commitBarriers();

    m_cmdList->ExecuteIndirect(
        m_device->dispatchIndirectCmdSignature,
        (UINT)1,
        argBuffer->m_resource,
        (UINT64)cmd.argBuffer.offset,
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

    bool updatePipeline = !m_rayTracingStateValid || cmd.pipeline != m_rayTracingPipeline;
    bool updateBindings = updatePipeline || cmd.bindingData != m_bindingData;
    bool updateShaderTable = updatePipeline || cmd.shaderTable != m_shaderTable;

    if (updatePipeline)
    {
        m_rayTracingPipeline = checked_cast<RayTracingPipelineImpl*>(cmd.pipeline);
        m_cmdList->SetComputeRootSignature(m_rayTracingPipeline->m_rootObjectLayout->m_rootSignature);
        m_cmdList4->SetPipelineState1(m_rayTracingPipeline->m_stateObject);
    }

    if (updateBindings)
    {
        m_bindingData = static_cast<BindingDataImpl*>(cmd.bindingData);
        setBindings(m_bindingData, BindMode::RayTracing);
    }

    if (updateShaderTable)
    {
        m_shaderTable = checked_cast<ShaderTableImpl*>(cmd.shaderTable);

        BufferImpl* shaderTableBuffer = m_shaderTable->getBuffer(m_rayTracingPipeline);
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

    m_renderStateValid = false;
    m_computeStateValid = false;
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
    BufferImpl* scratchBuffer = checked_cast<BufferImpl*>(cmd.scratchBuffer.buffer);

    requireBufferState(dst->m_buffer, ResourceState::AccelerationStructureWrite);
    if (src)
        requireBufferState(src->m_buffer, ResourceState::AccelerationStructureRead);
    requireBufferState(scratchBuffer, ResourceState::UnorderedAccess);

    for (uint32_t inputIndex = 0; inputIndex < cmd.desc.inputCount; ++inputIndex)
    {
        const AccelerationStructureBuildInput& input = cmd.desc.inputs[inputIndex];
        switch (input.type)
        {
        case AccelerationStructureBuildInputType::Instances:
            if (input.instances.instanceBuffer.buffer)
            {
                requireBufferState(
                    checked_cast<BufferImpl*>(input.instances.instanceBuffer.buffer),
                    ResourceState::AccelerationStructureBuildInput
                );
            }
            break;
        case AccelerationStructureBuildInputType::Triangles:
            for (uint32_t i = 0; i < input.triangles.vertexBufferCount; ++i)
            {
                if (input.triangles.vertexBuffers[i].buffer)
                {
                    requireBufferState(
                        checked_cast<BufferImpl*>(input.triangles.vertexBuffers[i].buffer),
                        ResourceState::AccelerationStructureBuildInput
                    );
                }
            }
            if (input.triangles.indexBuffer.buffer)
            {
                requireBufferState(
                    checked_cast<BufferImpl*>(input.triangles.indexBuffer.buffer),
                    ResourceState::AccelerationStructureBuildInput
                );
            }
            if (input.triangles.preTransformBuffer.buffer)
            {
                requireBufferState(
                    checked_cast<BufferImpl*>(input.triangles.preTransformBuffer.buffer),
                    ResourceState::AccelerationStructureBuildInput
                );
            }
            break;
        case AccelerationStructureBuildInputType::ProceduralPrimitives:
            for (uint32_t i = 0; i < input.proceduralPrimitives.aabbBufferCount; ++i)
            {
                if (input.proceduralPrimitives.aabbBuffers[i].buffer)
                {
                    requireBufferState(
                        checked_cast<BufferImpl*>(input.proceduralPrimitives.aabbBuffers[i].buffer),
                        ResourceState::AccelerationStructureBuildInput
                    );
                }
            }
            break;
        case AccelerationStructureBuildInputType::Spheres:
            for (uint32_t i = 0; i < input.spheres.vertexBufferCount; ++i)
            {
                if (input.spheres.vertexPositionBuffers[i].buffer)
                {
                    requireBufferState(
                        checked_cast<BufferImpl*>(input.spheres.vertexPositionBuffers[i].buffer),
                        ResourceState::AccelerationStructureBuildInput
                    );
                }
                if (input.spheres.vertexRadiusBuffers[i].buffer)
                {
                    requireBufferState(
                        checked_cast<BufferImpl*>(input.spheres.vertexRadiusBuffers[i].buffer),
                        ResourceState::AccelerationStructureBuildInput
                    );
                }
            }
            if (input.spheres.indexBuffer.buffer)
            {
                requireBufferState(
                    checked_cast<BufferImpl*>(input.spheres.indexBuffer.buffer),
                    ResourceState::AccelerationStructureBuildInput
                );
            }
            break;
        case AccelerationStructureBuildInputType::LinearSweptSpheres:
            for (uint32_t i = 0; i < input.linearSweptSpheres.vertexBufferCount; ++i)
            {
                if (input.linearSweptSpheres.vertexPositionBuffers[i].buffer)
                {
                    requireBufferState(
                        checked_cast<BufferImpl*>(input.linearSweptSpheres.vertexPositionBuffers[i].buffer),
                        ResourceState::AccelerationStructureBuildInput
                    );
                }
                if (input.linearSweptSpheres.vertexRadiusBuffers[i].buffer)
                {
                    requireBufferState(
                        checked_cast<BufferImpl*>(input.linearSweptSpheres.vertexRadiusBuffers[i].buffer),
                        ResourceState::AccelerationStructureBuildInput
                    );
                }
            }
            if (input.linearSweptSpheres.indexBuffer.buffer)
            {
                requireBufferState(
                    checked_cast<BufferImpl*>(input.linearSweptSpheres.indexBuffer.buffer),
                    ResourceState::AccelerationStructureBuildInput
                );
            }
            break;
        }
    }

    commitBarriers();

#if SLANG_RHI_ENABLE_NVAPI
    if (m_device->m_nvapiEnabled)
    {
        NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC_EX desc = {};
        desc.destAccelerationStructureData = dst->getDeviceAddress();
        desc.sourceAccelerationStructureData = src ? src->getDeviceAddress() : 0;
        desc.scratchAccelerationStructureData = cmd.scratchBuffer.getDeviceAddress();

        AccelerationStructureBuildDescConverterNVAPI converter;
        if (converter.convert(cmd.desc, m_device->m_debugCallback) != SLANG_OK)
            return;
        desc.inputs = converter.desc;

        std::vector<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC> postBuildInfoDescs;
        translatePostBuildInfoDescs(cmd.propertyQueryCount, cmd.queryDescs, postBuildInfoDescs);

        NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS params = {};
        params.version = NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS_VER;
        params.pDesc = &desc;
        params.pPostbuildInfoDescs = postBuildInfoDescs.data();
        params.numPostbuildInfoDescs = (NvU32)postBuildInfoDescs.size();
        NvAPI_D3D12_BuildRaytracingAccelerationStructureEx(m_cmdList4, &params);
    }
    else
#endif // SLANG_RHI_ENABLE_NVAPI
    {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
        buildDesc.DestAccelerationStructureData = dst->getDeviceAddress();
        buildDesc.SourceAccelerationStructureData = src ? src->getDeviceAddress() : 0;
        buildDesc.ScratchAccelerationStructureData = cmd.scratchBuffer.getDeviceAddress();

        AccelerationStructureBuildDescConverter converter;
        if (converter.convert(cmd.desc, m_device->m_debugCallback) != SLANG_OK)
            return;
        buildDesc.Inputs = converter.desc;

        std::vector<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC> postBuildInfoDescs;
        translatePostBuildInfoDescs(cmd.propertyQueryCount, cmd.queryDescs, postBuildInfoDescs);

        m_cmdList4->BuildRaytracingAccelerationStructure(&buildDesc, cmd.propertyQueryCount, postBuildInfoDescs.data());
    }
}

void CommandRecorder::cmdCopyAccelerationStructure(const commands::CopyAccelerationStructure& cmd)
{
    AccelerationStructureImpl* dst = checked_cast<AccelerationStructureImpl*>(cmd.dst);
    AccelerationStructureImpl* src = checked_cast<AccelerationStructureImpl*>(cmd.src);

    requireBufferState(dst->m_buffer, ResourceState::AccelerationStructureWrite);
    requireBufferState(src->m_buffer, ResourceState::AccelerationStructureRead);
    commitBarriers();

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
    for (uint32_t i = 0; i < cmd.accelerationStructureCount; i++)
        asAddresses[i] = cmd.accelerationStructures[i]->getDeviceAddress();
    translatePostBuildInfoDescs(cmd.queryCount, cmd.queryDescs, postBuildInfoDescs);
    m_cmdList4->EmitRaytracingAccelerationStructurePostbuildInfo(
        postBuildInfoDescs.data(),
        cmd.accelerationStructureCount,
        asAddresses.data()
    );
}

void CommandRecorder::cmdSerializeAccelerationStructure(const commands::SerializeAccelerationStructure& cmd)
{
    BufferImpl* dstBuffer = checked_cast<BufferImpl*>(cmd.dst.buffer);
    AccelerationStructureImpl* src = checked_cast<AccelerationStructureImpl*>(cmd.src);

    requireBufferState(dstBuffer, ResourceState::UnorderedAccess);
    requireBufferState(src->m_buffer, ResourceState::AccelerationStructureRead);
    commitBarriers();

    m_cmdList4->CopyRaytracingAccelerationStructure(
        cmd.dst.getDeviceAddress(),
        src->getDeviceAddress(),
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_SERIALIZE
    );
}

void CommandRecorder::cmdDeserializeAccelerationStructure(const commands::DeserializeAccelerationStructure& cmd)
{
    AccelerationStructureImpl* dst = checked_cast<AccelerationStructureImpl*>(cmd.dst);
    BufferImpl* srcBuffer = checked_cast<BufferImpl*>(cmd.src.buffer);

    requireBufferState(dst->m_buffer, ResourceState::AccelerationStructureWrite);
    requireBufferState(srcBuffer, ResourceState::ShaderResource);
    commitBarriers();

    m_cmdList4->CopyRaytracingAccelerationStructure(
        dst->getDeviceAddress(),
        cmd.src.getDeviceAddress(),
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_DESERIALIZE
    );
}

void CommandRecorder::cmdExecuteClusterOperation(const commands::ExecuteClusterOperation& cmd)
{
#if SLANG_RHI_ENABLE_NVAPI
    if (!m_device->m_nvapiEnabled)
        return;

    const ClusterOperationDesc& desc = cmd.desc;

    if (desc.params.maxArgCount == 0)
        return;

    BufferImpl* argCountBuffer = checked_cast<BufferImpl*>(desc.argCountBuffer.buffer);
    BufferImpl* argsBuffer = checked_cast<BufferImpl*>(desc.argsBuffer.buffer);
    BufferImpl* scratchBuffer = checked_cast<BufferImpl*>(desc.scratchBuffer.buffer);
    BufferImpl* addressesBuffer = checked_cast<BufferImpl*>(desc.addressesBuffer.buffer);
    BufferImpl* resultBuffer = checked_cast<BufferImpl*>(desc.resultBuffer.buffer);
    BufferImpl* sizesBuffer = checked_cast<BufferImpl*>(desc.sizesBuffer.buffer);

    requireBufferState(argsBuffer, ResourceState::ShaderResource);
    if (argCountBuffer)
        requireBufferState(argCountBuffer, ResourceState::ShaderResource);
    requireBufferState(scratchBuffer, ResourceState::UnorderedAccess);
    if (addressesBuffer)
        requireBufferState(addressesBuffer, ResourceState::UnorderedAccess);
    if (resultBuffer)
        requireBufferState(resultBuffer, ResourceState::AccelerationStructureWrite);
    if (sizesBuffer)
        requireBufferState(sizesBuffer, ResourceState::UnorderedAccess);
    commitBarriers();

    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC nvapiDesc = {};
    nvapiDesc.inputs = translateClusterOperationParams(desc.params);
    nvapiDesc.addressResolutionFlags =
        NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_NONE;

    // Input buffers
    if (desc.argCountBuffer)
    {
        nvapiDesc.indirectArgCount = desc.argCountBuffer.getDeviceAddress();
    }
    if (desc.argsBuffer)
    {
        nvapiDesc.indirectArgArray.StartAddress = desc.argsBuffer.getDeviceAddress();
        nvapiDesc.indirectArgArray.StrideInBytes = desc.argsBufferStride;
    }
    nvapiDesc.batchScratchData = desc.scratchBuffer.getDeviceAddress();

    // Input / output buffers
    if (desc.addressesBuffer)
    {
        nvapiDesc.destinationAddressArray.StartAddress = desc.addressesBuffer.getDeviceAddress();
        nvapiDesc.destinationAddressArray.StrideInBytes = desc.addressesBufferStride;
    }

    // Output buffers
    if (desc.resultBuffer)
    {
        nvapiDesc.batchResultData = desc.resultBuffer.getDeviceAddress();
    }
    if (sizesBuffer)
    {
        nvapiDesc.resultSizeArray.StartAddress = desc.sizesBuffer.getDeviceAddress();
        nvapiDesc.resultSizeArray.StrideInBytes = desc.sizesBufferStride;
    }

    NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS clusterOpParams = {};
    clusterOpParams.version = NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER;
    clusterOpParams.pDesc = &nvapiDesc;

    SLANG_RHI_NVAPI_CHECK(NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(m_cmdList4, &clusterOpParams));

#else  // SLANG_RHI_ENABLE_NVAPI
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(executeClusterOperation);
#endif // SLANG_RHI_ENABLE_NVAPI
}

void CommandRecorder::cmdConvertCooperativeVectorMatrix(const commands::ConvertCooperativeVectorMatrix& cmd)
{
#if SLANG_RHI_ENABLE_NVAPI
    BufferImpl* dstBuffer = checked_cast<BufferImpl*>(cmd.dstBuffer);
    BufferImpl* srcBuffer = checked_cast<BufferImpl*>(cmd.srcBuffer);

    requireBufferState(dstBuffer, ResourceState::UnorderedAccess);
    requireBufferState(srcBuffer, ResourceState::ShaderResource);
    commitBarriers();

    short_vector<NVAPI_CONVERT_COOPERATIVE_VECTOR_MATRIX_DESC> nvDescs;
    for (uint32_t i = 0; i < cmd.matrixCount; i++)
    {
        const CooperativeVectorMatrixDesc& dstDesc = cmd.dstDescs[i];
        const CooperativeVectorMatrixDesc& srcDesc = cmd.srcDescs[i];
        NVAPI_CONVERT_COOPERATIVE_VECTOR_MATRIX_DESC nvDesc = {};
        nvDesc.version = NVAPI_CONVERT_COOPERATIVE_VECTOR_MATRIX_DESC_VER1;
        nvDesc.srcSize = srcDesc.size;
        nvDesc.srcData.bIsDeviceAlloc = true;
        nvDesc.srcData.deviceAddress = srcBuffer->getDeviceAddress() + srcDesc.offset;
        nvDesc.pDstSize = (size_t*)&dstDesc.size;
        nvDesc.dstData.bIsDeviceAlloc = true;
        nvDesc.dstData.deviceAddress = dstBuffer->getDeviceAddress() + dstDesc.offset;
        nvDesc.srcComponentType = translateCooperativeVectorComponentType(srcDesc.componentType);
        nvDesc.dstComponentType = translateCooperativeVectorComponentType(dstDesc.componentType);
        nvDesc.numRows = srcDesc.rowCount;
        nvDesc.numColumns = srcDesc.colCount;
        nvDesc.srcLayout = translateCooperativeVectorMatrixLayout(srcDesc.layout);
        nvDesc.srcStride = srcDesc.rowColumnStride;
        nvDesc.dstLayout = translateCooperativeVectorMatrixLayout(dstDesc.layout);
        nvDesc.dstStride = dstDesc.rowColumnStride;
        nvDescs.push_back(nvDesc);
    }
    SLANG_RHI_NVAPI_CHECK(NvAPI_D3D12_ConvertCooperativeVectorMatrixMultiple(
        m_device->m_device,
        m_cmdList,
        nvDescs.data(),
        (NvU32)nvDescs.size()
    ));

    requireBufferState(dstBuffer, ResourceState::ShaderResource);
    commitBarriers();
#else
    SLANG_UNUSED(cmd);
#endif
}

void CommandRecorder::cmdSetBufferState(const commands::SetBufferState& cmd)
{
    m_stateTracking.setBufferState(checked_cast<BufferImpl*>(cmd.buffer), cmd.state);
}

void CommandRecorder::cmdSetTextureState(const commands::SetTextureState& cmd)
{
    m_stateTracking.setTextureState(checked_cast<TextureImpl*>(cmd.texture), cmd.subresourceRange, cmd.state);
}

void CommandRecorder::cmdGlobalBarrier(const commands::GlobalBarrier& cmd)
{
    // Global barrier on D3D12 is implemented with a UAV barrier pointing at null resource.
    // https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12
    // TODO: Look at using D3D12 advanced barriers when available, currently only experimental in agility sdk though.
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = nullptr;
    m_cmdList->ResourceBarrier(1, &barrier);
}

void CommandRecorder::cmdPushDebugGroup(const commands::PushDebugGroup& cmd)
{
    auto beginEvent = m_device->m_BeginEventOnCommandList;
    if (beginEvent)
    {
        UINT64 color = 0xff000000;
        color |= uint8_t(cmd.color.r * 255.0f) << 16;
        color |= uint8_t(cmd.color.g * 255.0f) << 8;
        color |= uint8_t(cmd.color.b * 255.0f);
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
        color |= uint8_t(cmd.color.r * 255.0f) << 16;
        color |= uint8_t(cmd.color.g * 255.0f) << 8;
        color |= uint8_t(cmd.color.b * 255.0f);
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

void CommandRecorder::setBindings(BindingDataImpl* bindingData, BindMode bindMode)
{
    // First, we transition all resources to the required states.
    for (uint32_t i = 0; i < bindingData->bufferStateCount; ++i)
    {
        const auto& bufferState = bindingData->bufferStates[i];
        requireBufferState(bufferState.buffer, bufferState.state);
    }
    for (uint32_t i = 0; i < bindingData->textureStateCount; ++i)
    {
        const auto& textureState = bindingData->textureStates[i];
        requireTextureState(
            textureState.textureView->m_texture,
            textureState.textureView->m_desc.subresourceRange,
            textureState.state
        );
    }
    commitBarriers();

    // Then we bind the root parameters.
    if (bindMode == BindMode::Graphics)
    {
        for (uint32_t i = 0; i < bindingData->rootParameterCount; ++i)
        {
            const auto& param = bindingData->rootParameters[i];
            switch (param.type)
            {
            case BindingDataImpl::RootParameter::CBV:
                m_cmdList->SetGraphicsRootConstantBufferView(param.index, param.bufferLocation);
                break;
            case BindingDataImpl::RootParameter::UAV:
                m_cmdList->SetGraphicsRootUnorderedAccessView(param.index, param.bufferLocation);
                break;
            case BindingDataImpl::RootParameter::SRV:
                m_cmdList->SetGraphicsRootShaderResourceView(param.index, param.bufferLocation);
                break;
            case BindingDataImpl::RootParameter::DescriptorTable:
                m_cmdList->SetGraphicsRootDescriptorTable(param.index, param.baseDescriptor);
                break;
            }
        }
    }
    else if (bindMode == BindMode::Compute || bindMode == BindMode::RayTracing)
    {
        for (uint32_t i = 0; i < bindingData->rootParameterCount; ++i)
        {
            const auto& param = bindingData->rootParameters[i];
            switch (param.type)
            {
            case BindingDataImpl::RootParameter::CBV:
                m_cmdList->SetComputeRootConstantBufferView(param.index, param.bufferLocation);
                break;
            case BindingDataImpl::RootParameter::UAV:
                m_cmdList->SetComputeRootUnorderedAccessView(param.index, param.bufferLocation);
                break;
            case BindingDataImpl::RootParameter::SRV:
                m_cmdList->SetComputeRootShaderResourceView(param.index, param.bufferLocation);
                break;
            case BindingDataImpl::RootParameter::DescriptorTable:
                m_cmdList->SetComputeRootDescriptorTable(param.index, param.baseDescriptor);
                break;
            }
        }
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
    if (testing::gDebugDisableStateTracking)
        return;

    short_vector<D3D12_RESOURCE_BARRIER, 16> barriers;

    for (const auto& bufferBarrier : m_stateTracking.getBufferBarriers())
    {
        BufferImpl* buffer = checked_cast<BufferImpl*>(bufferBarrier.buffer);
        D3D12_RESOURCE_BARRIER barrier = {};
        D3D12_RESOURCE_STATES stateBefore = translateResourceState(bufferBarrier.stateBefore);
        D3D12_RESOURCE_STATES stateAfter = translateResourceState(bufferBarrier.stateAfter);
        // Acceleration structure buffers need to be treated specially.
        // D3D12 doesn't allow to transition to/from D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE state.
        // Instead, UAV barriers are used to synchronize accesses.
        if (stateBefore != stateAfter &&
            ((stateBefore & D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE) == 0) &&
            ((stateAfter & D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE) == 0))
        {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = buffer->m_resource;
            barrier.Transition.StateBefore = stateBefore;
            barrier.Transition.StateAfter = stateAfter;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers.push_back(barrier);
        }
        else if ((bufferBarrier.stateBefore == ResourceState::AccelerationStructureWrite &&
                  bufferBarrier.stateAfter == ResourceState::AccelerationStructureRead) ||
                 (bufferBarrier.stateAfter == ResourceState::AccelerationStructureRead &&
                  bufferBarrier.stateBefore == ResourceState::AccelerationStructureWrite) ||
                 ((stateAfter & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0))
        {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = buffer->m_resource;
            barriers.push_back(barrier);
        }
    }

    for (const auto& textureBarrier : m_stateTracking.getTextureBarriers())
    {
        TextureImpl* texture = checked_cast<TextureImpl*>(textureBarrier.texture);
        D3D12_RESOURCE_BARRIER barrier = {};
        D3D12_RESOURCE_STATES stateBefore = translateResourceState(textureBarrier.stateBefore);
        D3D12_RESOURCE_STATES stateAfter = translateResourceState(textureBarrier.stateAfter);
        if (stateBefore != stateAfter)
        {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = texture->m_resource;
            barrier.Transition.StateBefore = stateBefore;
            barrier.Transition.StateAfter = stateAfter;
            if (textureBarrier.entireTexture)
            {
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barriers.push_back(barrier);
            }
            else
            {
                uint32_t mipCount = texture->m_desc.mipCount;
                uint32_t layerCount = texture->m_desc.getLayerCount();
                DXGI_FORMAT d3dFormat = getMapFormat(texture->m_desc.format);
                uint32_t planeCount = getPlaneSliceCount(d3dFormat);
                for (uint32_t planeIndex = 0; planeIndex < planeCount; ++planeIndex)
                {
                    barrier.Transition.Subresource =
                        getSubresourceIndex(textureBarrier.mip, textureBarrier.layer, planeIndex, mipCount, layerCount);
                    barriers.push_back(barrier);
                }
            }
        }
        else if ((stateAfter & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0)
        {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = texture->m_resource;
            barriers.push_back(barrier);
        }
    }

    if (!barriers.empty())
    {
        m_cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
    }

    m_stateTracking.clearBarriers();
}

// CommandQueueImpl

CommandQueueImpl::CommandQueueImpl(Device* device, QueueType type)
    : CommandQueue(device, type)
{
}

CommandQueueImpl::~CommandQueueImpl()
{
    waitOnHost();
    ::CloseHandle(m_globalWaitHandle);
}

Result CommandQueueImpl::init(uint32_t queueIndex)
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    m_queueIndex = queueIndex;
    m_d3dDevice = device->m_device;
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
        commandBuffer->setInternalReferenceCount(0);
    }
    returnRefPtr(outCommandBuffer, commandBuffer);
    return SLANG_OK;
}

void CommandQueueImpl::retireCommandBuffer(CommandBufferImpl* commandBuffer)
{
    commandBuffer->reset();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_commandBuffersPool.push_back(commandBuffer);
        commandBuffer->setInternalReferenceCount(1);
    }
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
            retireCommandBuffer(commandBuffer);
        }
        else
        {
            m_commandBuffersInFlight.push_back(commandBuffer);
        }
    }

    // Flush all device heaps
    getDevice<DeviceImpl>()->flushHeaps();
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

Result CommandQueueImpl::submit(const SubmitDesc& desc)
{
    // Increment last submitted ID which is used to track command buffer completion.
    ++m_lastSubmittedID;

    // Wait on fences.
    for (uint32_t i = 0; i < desc.waitFenceCount; ++i)
    {
        FenceImpl* fence = checked_cast<FenceImpl*>(desc.waitFences[i]);
        m_d3dQueue->Wait(fence->m_fence.get(), desc.waitFenceValues[i]);
    }

    // Execute command lists.
    short_vector<ID3D12CommandList*> commandLists;
    for (uint32_t i = 0; i < desc.commandBufferCount; i++)
    {
        CommandBufferImpl* commandBuffer = checked_cast<CommandBufferImpl*>(desc.commandBuffers[i]);
        commandBuffer->m_submissionID = m_lastSubmittedID;
        m_commandBuffersInFlight.push_back(commandBuffer);
        commandLists.push_back(commandBuffer->m_d3dCommandList);
    }
    if (commandLists.size() > 0)
    {
        m_d3dQueue->ExecuteCommandLists(commandLists.size(), commandLists.data());
    }

    // Signal fences.
    for (uint32_t i = 0; i < desc.signalFenceCount; ++i)
    {
        FenceImpl* fence = checked_cast<FenceImpl*>(desc.signalFences[i]);
        SLANG_RETURN_ON_FAIL(m_d3dQueue->Signal(fence->m_fence.get(), desc.signalFenceValues[i]));
    }

    m_d3dQueue->Signal(m_trackingFence.get(), m_lastSubmittedID);

    retireCommandBuffers();

    return SLANG_OK;
}

Result CommandQueueImpl::waitOnHost()
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    m_lastSubmittedID++;
    m_d3dQueue->Signal(m_trackingFence.get(), m_lastSubmittedID);
    ResetEvent(m_globalWaitHandle);
    m_trackingFence->SetEventOnCompletion(m_lastSubmittedID, m_globalWaitHandle);
    WaitForSingleObject(m_globalWaitHandle, INFINITE);
    device->flushValidationMessages();
    retireCommandBuffers();
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

CommandEncoderImpl::CommandEncoderImpl(Device* device, CommandQueueImpl* queue)
    : CommandEncoder(device)
    , m_queue(queue)
{
}

CommandEncoderImpl::~CommandEncoderImpl()
{
    // If the command buffer was not used, return it to the pool.
    if (m_commandBuffer)
    {
        // Need to close the d3d12 command list because we never recorded commands.
        m_commandBuffer->m_d3dCommandList->Close();
        m_queue->retireCommandBuffer(m_commandBuffer);
    }
}

Result CommandEncoderImpl::init()
{
    SLANG_RETURN_ON_FAIL(m_queue->getOrCreateCommandBuffer(m_commandBuffer.writeRef()));
    m_commandList = &m_commandBuffer->m_commandList;
    return SLANG_OK;
}

Result CommandEncoderImpl::getBindingData(RootShaderObject* rootObject, BindingData*& outBindingData)
{
    rootObject->trackResources(m_commandBuffer->m_trackedObjects);
    BindingDataBuilder builder;
    builder.m_device = getDevice<DeviceImpl>();
    builder.m_allocator = &m_commandBuffer->m_allocator;
    builder.m_bindingCache = &m_commandBuffer->m_bindingCache;
    builder.m_constantBufferPool = &m_commandBuffer->m_constantBufferPool;
    builder.m_cbvSrvUavArena = &m_commandBuffer->m_cbvSrvUavArena;
    builder.m_samplerArena = &m_commandBuffer->m_samplerArena;
    ShaderObjectLayout* specializedLayout = nullptr;
    SLANG_RETURN_ON_FAIL(rootObject->getSpecializedLayout(specializedLayout));
    return builder.bindAsRoot(
        rootObject,
        checked_cast<RootShaderObjectLayoutImpl*>(specializedLayout),
        (BindingDataImpl*&)outBindingData
    );
}

Result CommandEncoderImpl::finish(ICommandBuffer** outCommandBuffer)
{
    SLANG_RETURN_ON_FAIL(resolvePipelines(m_device));
    CommandRecorder recorder(getDevice<DeviceImpl>());
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

CommandBufferImpl::CommandBufferImpl(Device* device, CommandQueueImpl* queue)
    : CommandBuffer(device)
    , m_queue(queue)
{
}

CommandBufferImpl::~CommandBufferImpl() {}

Result CommandBufferImpl::init()
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    SLANG_RETURN_ON_FAIL(device->m_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(m_d3dCommandAllocator.writeRef())
    ));
    SLANG_RETURN_ON_FAIL(device->m_device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_d3dCommandAllocator,
        nullptr,
        IID_PPV_ARGS(m_d3dCommandList.writeRef())
    ));

    ID3D12DescriptorHeap* heaps[] = {
        device->m_gpuCbvSrvUavHeap->getHeap(),
        device->m_gpuSamplerHeap->getHeap(),
    };
    m_d3dCommandList->SetDescriptorHeaps(SLANG_COUNT_OF(heaps), heaps);

    m_constantBufferPool.init(device);

    SLANG_RETURN_ON_FAIL(m_cbvSrvUavArena.init(device->m_gpuCbvSrvUavHeap, 128));
    SLANG_RETURN_ON_FAIL(m_samplerArena.init(device->m_gpuSamplerHeap, 4));

    return SLANG_OK;
}

Result CommandBufferImpl::reset()
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    SLANG_RETURN_ON_FAIL(m_d3dCommandAllocator->Reset());
    SLANG_RETURN_ON_FAIL(m_d3dCommandList->Reset(m_d3dCommandAllocator, nullptr));
    ID3D12DescriptorHeap* heaps[] = {
        device->m_gpuCbvSrvUavHeap->getHeap(),
        device->m_gpuSamplerHeap->getHeap(),
    };
    m_d3dCommandList->SetDescriptorHeaps(SLANG_COUNT_OF(heaps), heaps);

    m_cbvSrvUavArena.reset();
    m_samplerArena.reset();
    m_constantBufferPool.reset();
    m_bindingCache.reset();
    return CommandBuffer::reset();
}

Result CommandBufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12GraphicsCommandList;
    outHandle->value = (uint64_t)m_d3dCommandList.get();
    return SLANG_OK;
}

} // namespace rhi::d3d12
