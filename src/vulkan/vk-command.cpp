#include "vk-command.h"
#include "vk-device.h"
#include "vk-buffer.h"
#include "vk-texture.h"
#include "vk-fence.h"
#include "vk-query.h"
#include "vk-acceleration-structure.h"
#include "vk-shader-table.h"
#include "vk-pipeline.h"
#include "vk-utils.h"
#include "vk-shader-object.h"
#include "vk-shader-object-layout.h"
#include "../command-list.h"
#include "../state-tracking.h"
#include "../strings.h"

#include "core/static_vector.h"

namespace rhi::vk {

template<typename T>
inline bool arraysEqual(uint32_t countA, uint32_t countB, const T* a, const T* b)
{
    return (countA == countB) ? std::memcmp(a, b, countA * sizeof(T)) == 0 : false;
}

class CommandRecorder
{
public:
    DeviceImpl* m_device;
    VulkanApi& m_api;

    VkCommandBuffer m_cmdBuffer;

    StateTracking m_stateTracking;

    short_vector<RefPtr<TextureViewImpl>> m_renderTargetViews;
    short_vector<RefPtr<TextureViewImpl>> m_resolveTargetViews;
    RefPtr<TextureViewImpl> m_depthStencilView;

    bool m_renderPassActive = false;
    bool m_renderStateValid = false;
    RenderState m_renderState;
    RefPtr<RenderPipelineImpl> m_renderPipeline;

    bool m_preparedRenderStateValid = false;
    RenderState m_preparedRenderState;
    BindingDataImpl* m_preparedRenderBindingData = nullptr;

    bool m_computePassActive = false;
    bool m_computeStateValid = false;
    RefPtr<ComputePipelineImpl> m_computePipeline;

    bool m_rayTracingPassActive = false;
    bool m_rayTracingStateValid = false;
    RefPtr<RayTracingPipelineImpl> m_rayTracingPipeline;
    RefPtr<ShaderTableImpl> m_shaderTable;

    BindingDataImpl* m_bindingData = nullptr;

    uint64_t m_rayGenTableAddr = 0;
    VkStridedDeviceAddressRegionKHR m_raygenSBT;
    VkStridedDeviceAddressRegionKHR m_missSBT;
    VkStridedDeviceAddressRegionKHR m_hitSBT;
    VkStridedDeviceAddressRegionKHR m_callableSBT;

    CommandRecorder(DeviceImpl* device)
        : m_device(device)
        , m_api(device->m_api)
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
    void cmdConvertCooperativeVectorMatrix(const commands::ConvertCooperativeVectorMatrix& cmd);
    void cmdSetBufferState(const commands::SetBufferState& cmd);
    void cmdSetTextureState(const commands::SetTextureState& cmd);
    void cmdGlobalBarrier(const commands::GlobalBarrier& cmd);
    void cmdPushDebugGroup(const commands::PushDebugGroup& cmd);
    void cmdPopDebugGroup(const commands::PopDebugGroup& cmd);
    void cmdInsertDebugMarker(const commands::InsertDebugMarker& cmd);
    void cmdWriteTimestamp(const commands::WriteTimestamp& cmd);
    void cmdExecuteCallback(const commands::ExecuteCallback& cmd);

    void prepareSetRenderState(const commands::SetRenderState& cmd);

    void setBindings(BindingDataImpl* bindingData, VkPipelineBindPoint bindPoint);

    void requireBindingStates(BindingDataImpl* bindingData);
    void requireBufferState(BufferImpl* buffer, ResourceState state);
    void requireTextureState(TextureImpl* texture, SubresourceRange subresourceRange, ResourceState state);
    void commitBarriers();

    void queryAccelerationStructureProperties(
        uint32_t accelerationStructureCount,
        IAccelerationStructure* const* accelerationStructures,
        uint32_t queryCount,
        const AccelerationStructureQueryDesc* queryDescs
    );

    void accelerationStructureBarrier(
        uint32_t accelerationStructureCount,
        IAccelerationStructure* const* accelerationStructures,
        AccessFlag srcAccess,
        AccessFlag destAccess
    );
};

Result CommandRecorder::record(CommandBufferImpl* commandBuffer)
{
    m_cmdBuffer = commandBuffer->m_commandBuffer;

    VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    SLANG_VK_RETURN_ON_FAIL(m_api.vkBeginCommandBuffer(m_cmdBuffer, &beginInfo));

    CommandList& commandList = commandBuffer->m_commandList;

    for (const CommandList::CommandSlot* slot = commandList.getCommands(); slot; slot = slot->next)
    {
        // Vulkan generally does not allow barrier commands to be recorded inside a render pass.
        // To work around this, we collect all barriers needed for the render pass before entering it.
        // We can do so by checking all SetRenderState commands inside the render pass, so we can queue
        // the barrier commands before entering the render pass.
        //
        if (slot->id == CommandID::BeginRenderPass)
        {
            for (auto subCmdSlot = slot->next; subCmdSlot; subCmdSlot = subCmdSlot->next)
            {
                if (subCmdSlot->id == CommandID::SetRenderState)
                {
                    prepareSetRenderState(commandList.getCommand<commands::SetRenderState>(subCmdSlot));
                }
                else if (subCmdSlot->id == CommandID::EndRenderPass)
                {
                    break;
                }
            }
            commitBarriers();
        }

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

    SLANG_VK_RETURN_ON_FAIL(m_api.vkEndCommandBuffer(m_cmdBuffer));

    return SLANG_OK;
}

#define NOT_SUPPORTED(x) m_device->printWarning(x " command is not supported!")

void CommandRecorder::cmdCopyBuffer(const commands::CopyBuffer& cmd)
{
    BufferImpl* dst = checked_cast<BufferImpl*>(cmd.dst);
    BufferImpl* src = checked_cast<BufferImpl*>(cmd.src);

    requireBufferState(dst, ResourceState::CopyDestination);
    requireBufferState(src, ResourceState::CopySource);
    commitBarriers();

    VkBufferCopy copyRegion;
    copyRegion.dstOffset = cmd.dstOffset;
    copyRegion.srcOffset = cmd.srcOffset;
    copyRegion.size = cmd.size;

    m_api.vkCmdCopyBuffer(m_cmdBuffer, src->m_buffer.m_buffer, dst->m_buffer.m_buffer, 1, &copyRegion);
}

void CommandRecorder::cmdCopyTexture(const commands::CopyTexture& cmd)
{
    TextureImpl* dst = checked_cast<TextureImpl*>(cmd.dst);
    TextureImpl* src = checked_cast<TextureImpl*>(cmd.src);
    SubresourceRange dstSubresource = cmd.dstSubresource;
    Offset3D dstOffset = cmd.dstOffset;
    SubresourceRange srcSubresource = cmd.srcSubresource;
    Offset3D srcOffset = cmd.srcOffset;
    Extent3D extent = cmd.extent;

    // Fix up sub resource ranges.
    if (dstSubresource.layerCount == 0)
        dstSubresource.layerCount = dst->m_desc.getLayerCount();
    if (dstSubresource.mipCount == 0)
        dstSubresource.mipCount = dst->m_desc.mipCount;
    if (srcSubresource.layerCount == 0)
        srcSubresource.layerCount = src->m_desc.getLayerCount();
    if (srcSubresource.mipCount == 0)
        srcSubresource.mipCount = src->m_desc.mipCount;

    requireTextureState(dst, dstSubresource, ResourceState::CopyDestination);
    requireTextureState(src, srcSubresource, ResourceState::CopySource);
    commitBarriers();

    // TODO: Could probably optimize this to do:
    //  - A single copy of the extents are fixed
    //  - Batching copies at the same mip level if extents aren't fixed.
    Extent3D srcTextureSize = src->m_desc.size;
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

            VkImageLayout srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            VkImageLayout dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

            VkImageCopy region = {};
            region.srcSubresource.aspectMask = getAspectMaskFromFormat(src->m_vkformat);
            region.srcSubresource.baseArrayLayer = srcSubresource.layer + layer;
            region.srcSubresource.mipLevel = srcMip;
            region.srcSubresource.layerCount = 1;
            region.srcOffset = {(int32_t)srcOffset.x, (int32_t)srcOffset.y, (int32_t)srcOffset.z};
            region.dstSubresource.aspectMask = getAspectMaskFromFormat(dst->m_vkformat);
            region.dstSubresource.baseArrayLayer = dstSubresource.layer + layer;
            region.dstSubresource.mipLevel = dstMip;
            region.dstSubresource.layerCount = 1;
            region.dstOffset = {(int32_t)dstOffset.x, (int32_t)dstOffset.y, (int32_t)dstOffset.z};
            region.extent = {adjustedExtent.width, adjustedExtent.height, adjustedExtent.depth};

            m_api.vkCmdCopyImage(m_cmdBuffer, src->m_image, srcImageLayout, dst->m_image, dstImageLayout, 1, &region);
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

    // Vulkan specifically doesn't require aligning to block size (and will throw errors if it makes extents
    // too large for the smaller mip levels).
    // adjustedExtent.width = math::calcAligned(adjustedExtent.width, formatInfo.blockWidth);
    // adjustedExtent.height = math::calcAligned(adjustedExtent.height, formatInfo.blockHeight);

    // Calculate the row length (in texels) from the supplied pitch (in bytes)
    uint32_t rowLengthInBlocks = dstRowPitch / formatInfo.blockSizeInBytes;
    uint32_t rowLengthInTexels = rowLengthInBlocks * formatInfo.blockWidth;

    // Setup region copy args.
    VkBufferImageCopy region = {};
    region.bufferOffset = dstOffset;
    region.bufferRowLength = rowLengthInTexels;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = getAspectMaskFromFormat(src->m_vkformat);
    region.imageSubresource.mipLevel = srcMip;
    region.imageSubresource.baseArrayLayer = srcLayer;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {(int32_t)srcOffset.x, (int32_t)srcOffset.y, (int32_t)srcOffset.z};
    region.imageExtent = {adjustedExtent.width, adjustedExtent.height, adjustedExtent.depth};

    m_api.vkCmdCopyImageToBuffer(
        m_cmdBuffer,
        src->m_image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dst->m_buffer.m_buffer,
        1,
        &region
    );
}

void CommandRecorder::cmdClearBuffer(const commands::ClearBuffer& cmd)
{
    BufferImpl* buffer = checked_cast<BufferImpl*>(cmd.buffer);

    requireBufferState(buffer, ResourceState::CopyDestination);
    commitBarriers();

    VkDeviceSize offset = cmd.range.offset;
    VkDeviceSize size = cmd.range.size;

    // Handle Vulkan buffer size requirement: If size is not equal to
    // VK_WHOLE_SIZE, size must be a multiple of 4
    // (https://vulkan.lunarg.com/doc/view/1.4.304.0/windows/1.4-extensions/vkspec.html#VUID-vkCmdFillBuffer-size-00028)
    // If user explicitly requests to fill the whole buffer, automatically
    // use the VK_WHOLE_SIZE constant to give same functionality as other
    // targets.
    if (offset == 0 && size == buffer->m_desc.size)
        size = VK_WHOLE_SIZE;

    m_api.vkCmdFillBuffer(m_cmdBuffer, buffer->m_buffer.m_buffer, offset, size, 0);
}

void CommandRecorder::cmdClearTextureFloat(const commands::ClearTextureFloat& cmd)
{
    TextureImpl* texture = checked_cast<TextureImpl*>(cmd.texture);

    requireTextureState(texture, cmd.subresourceRange, ResourceState::CopyDestination);
    commitBarriers();

    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = cmd.subresourceRange.mip;
    subresourceRange.levelCount = cmd.subresourceRange.mipCount;
    subresourceRange.baseArrayLayer = cmd.subresourceRange.layer;
    subresourceRange.layerCount = cmd.subresourceRange.layerCount;

    VkClearColorValue vkClearColor = {};
    std::memcpy(vkClearColor.float32, cmd.clearValue, sizeof(float) * 4);

    m_api.vkCmdClearColorImage(
        m_cmdBuffer,
        texture->m_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &vkClearColor,
        1,
        &subresourceRange
    );
}

void CommandRecorder::cmdClearTextureUint(const commands::ClearTextureUint& cmd)
{
    TextureImpl* texture = checked_cast<TextureImpl*>(cmd.texture);

    requireTextureState(texture, cmd.subresourceRange, ResourceState::CopyDestination);
    commitBarriers();

    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = cmd.subresourceRange.mip;
    subresourceRange.levelCount = cmd.subresourceRange.mipCount;
    subresourceRange.baseArrayLayer = cmd.subresourceRange.layer;
    subresourceRange.layerCount = cmd.subresourceRange.layerCount;

    VkClearColorValue vkClearColor = {};
    std::memcpy(vkClearColor.uint32, cmd.clearValue, sizeof(uint32_t) * 4);

    m_api.vkCmdClearColorImage(
        m_cmdBuffer,
        texture->m_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &vkClearColor,
        1,
        &subresourceRange
    );
}

void CommandRecorder::cmdClearTextureDepthStencil(const commands::ClearTextureDepthStencil& cmd)
{
    TextureImpl* texture = checked_cast<TextureImpl*>(cmd.texture);
    const FormatInfo& formatInfo = getFormatInfo(texture->m_desc.format);

    requireTextureState(texture, cmd.subresourceRange, ResourceState::CopyDestination);
    commitBarriers();

    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = 0;
    subresourceRange.baseMipLevel = cmd.subresourceRange.mip;
    subresourceRange.levelCount = cmd.subresourceRange.mipCount;
    subresourceRange.baseArrayLayer = cmd.subresourceRange.layer;
    subresourceRange.layerCount = cmd.subresourceRange.layerCount;

    VkClearDepthStencilValue vkClearValue = {};
    vkClearValue.depth = cmd.depthValue;
    vkClearValue.stencil = cmd.stencilValue;

    subresourceRange.aspectMask = 0;
    if (formatInfo.hasDepth && cmd.clearDepth)
        subresourceRange.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if (formatInfo.hasStencil && cmd.clearStencil)
        subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

    m_api.vkCmdClearDepthStencilImage(
        m_cmdBuffer,
        texture->m_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &vkClearValue,
        1,
        &subresourceRange
    );
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

            // https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkBufferImageCopy.html
            // bufferRowLength and bufferImageHeight specify the data in buffer
            // memory as a subregion of a larger two- or three-dimensional image,
            // and control the addressing calculations of data in buffer memory. If
            // either of these values is zero, that aspect of the buffer memory is
            // considered to be tightly packed according to the imageExtent.

            // Calculate the row length (in texels) from the supplied pitch (in bytes)
            uint32_t rowLengthInBlocks = srLayout->rowPitch / srLayout->colPitch;
            uint32_t rowLengthInTexels = rowLengthInBlocks * srLayout->blockWidth;

            VkBufferImageCopy region = {};

            region.bufferOffset = bufferOffset;
            region.bufferRowLength = rowLengthInTexels;
            region.bufferImageHeight = 0;

            region.imageSubresource.aspectMask = getAspectMaskFromFormat(dst->m_vkformat);
            region.imageSubresource.mipLevel = mip;
            region.imageSubresource.baseArrayLayer = layer;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {int32_t(cmd.offset.x), int32_t(cmd.offset.y), int32_t(cmd.offset.z)};
            region.imageExtent = {srLayout->size.width, srLayout->size.height, srLayout->size.depth};

            m_api.vkCmdCopyBufferToImage(
                m_cmdBuffer,
                buffer->m_buffer.m_buffer,
                dst->m_image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &region
            );

            bufferOffset += srLayout->sizeInBytes;
            srLayout++;
        }
    }
}

void CommandRecorder::cmdResolveQuery(const commands::ResolveQuery& cmd)
{
    BufferImpl* buffer = checked_cast<BufferImpl*>(cmd.buffer);
    QueryPoolImpl* queryPool = checked_cast<QueryPoolImpl*>(cmd.queryPool);

    requireBufferState(buffer, ResourceState::CopyDestination);
    commitBarriers();

    m_api.vkCmdCopyQueryPoolResults(
        m_cmdBuffer,
        queryPool->m_pool,
        cmd.index,
        cmd.count,
        buffer->m_buffer.m_buffer,
        cmd.offset,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
    );
}

void CommandRecorder::cmdBeginRenderPass(const commands::BeginRenderPass& cmd)
{
    const RenderPassDesc& desc = cmd.desc;

    m_renderTargetViews.resize(desc.colorAttachmentCount);
    m_resolveTargetViews.resize(desc.colorAttachmentCount);
    short_vector<VkRenderingAttachmentInfoKHR> colorAttachmentInfos;
    VkRenderingAttachmentInfoKHR depthAttachmentInfo = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    VkRenderingAttachmentInfoKHR stencilAttachmentInfo = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    bool hasDepthAttachment = false;
    bool hasStencilAttachment = false;
    VkRect2D renderArea;
    renderArea.offset = {0, 0};
    renderArea.extent = {
        m_api.m_deviceProperties.limits.maxFramebufferWidth,
        m_api.m_deviceProperties.limits.maxFramebufferHeight
    };
    uint32_t layerCount = 1;

    for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i)
    {
        const auto& attachment = desc.colorAttachments[i];
        TextureViewImpl* view = checked_cast<TextureViewImpl*>(attachment.view);
        TextureViewImpl* resolveView = checked_cast<TextureViewImpl*>(attachment.resolveTarget);

        m_renderTargetViews[i] = view;
        m_resolveTargetViews[i] = resolveView;

        // Transition state
        requireTextureState(view->m_texture, view->m_desc.subresourceRange, ResourceState::RenderTarget);
        if (resolveView)
            requireTextureState(
                resolveView->m_texture,
                resolveView->m_desc.subresourceRange,
                ResourceState::ResolveDestination
            );

        // Determine render area
        const TextureViewDesc& viewDesc = view->m_desc;
        const TextureDesc& textureDesc = view->m_texture->m_desc;
        uint32_t width = calcMipSize(textureDesc.size.width, viewDesc.subresourceRange.mip);
        uint32_t height = calcMipSize(textureDesc.size.height, viewDesc.subresourceRange.mip);
        renderArea.extent.width = min(renderArea.extent.width, width);
        renderArea.extent.height = min(renderArea.extent.height, height);
        uint32_t attachmentLayerCount = (textureDesc.type == TextureType::Texture3D)
                                            ? textureDesc.size.depth
                                            : viewDesc.subresourceRange.layerCount;
        layerCount = max(layerCount, attachmentLayerCount);

        // Create attachment info
        VkRenderingAttachmentInfoKHR attachmentInfo = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
        attachmentInfo.imageView = checked_cast<TextureViewImpl*>(attachment.view)->getRenderTargetView().imageView;
        attachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        if (attachment.resolveTarget)
        {
            attachmentInfo.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            attachmentInfo.resolveImageView = resolveView->getRenderTargetView().imageView;
            attachmentInfo.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        attachmentInfo.loadOp = translateLoadOp(attachment.loadOp);
        attachmentInfo.storeOp = translateStoreOp(attachment.storeOp);
        attachmentInfo.clearValue.color.float32[0] = attachment.clearValue[0];
        attachmentInfo.clearValue.color.float32[1] = attachment.clearValue[1];
        attachmentInfo.clearValue.color.float32[2] = attachment.clearValue[2];
        attachmentInfo.clearValue.color.float32[3] = attachment.clearValue[3];
        colorAttachmentInfos.push_back(attachmentInfo);
    }

    // Transition depth stencil from its initial state to depth write state.
    if (desc.depthStencilAttachment)
    {
        const auto& attachment = *desc.depthStencilAttachment;
        TextureViewImpl* view = checked_cast<TextureViewImpl*>(attachment.view);

        m_depthStencilView = checked_cast<TextureViewImpl*>(desc.depthStencilAttachment->view);

        // Transition state
        requireTextureState(
            view->m_texture,
            view->m_desc.subresourceRange,
            attachment.depthReadOnly ? ResourceState::DepthRead : ResourceState::DepthWrite
        );

        // Determine render area
        const TextureViewDesc& viewDesc = view->m_desc;
        const TextureDesc& textureDesc = view->m_texture->m_desc;
        uint32_t width = calcMipSize(textureDesc.size.width, viewDesc.subresourceRange.mip);
        uint32_t height = calcMipSize(textureDesc.size.height, viewDesc.subresourceRange.mip);
        renderArea.extent.width = min(renderArea.extent.width, width);
        renderArea.extent.height = min(renderArea.extent.height, height);

        // Create attachment info
        if (isDepthFormat(view->m_texture->m_vkformat))
        {
            hasDepthAttachment = true;
            const auto& dsa = *desc.depthStencilAttachment;
            depthAttachmentInfo.imageView = checked_cast<TextureViewImpl*>(dsa.view)->getRenderTargetView().imageView;
            depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachmentInfo.loadOp = translateLoadOp(dsa.depthLoadOp);
            depthAttachmentInfo.storeOp = translateStoreOp(dsa.depthStoreOp);
            depthAttachmentInfo.clearValue.depthStencil.depth = dsa.depthClearValue;
        }
        if (isStencilFormat(view->m_texture->m_vkformat))
        {
            hasStencilAttachment = true;
            const auto& dsa = *desc.depthStencilAttachment;
            stencilAttachmentInfo.imageView = checked_cast<TextureViewImpl*>(dsa.view)->getRenderTargetView().imageView;
            stencilAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            stencilAttachmentInfo.loadOp = translateLoadOp(dsa.stencilLoadOp);
            stencilAttachmentInfo.storeOp = translateStoreOp(dsa.stencilStoreOp);
            stencilAttachmentInfo.clearValue.depthStencil.stencil = dsa.stencilClearValue;
        }
    }

    commitBarriers();

    VkRenderingInfoKHR renderingInfo = {VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    renderingInfo.renderArea = renderArea;
    renderingInfo.layerCount = layerCount;
    renderingInfo.colorAttachmentCount = colorAttachmentInfos.size();
    renderingInfo.pColorAttachments = colorAttachmentInfos.data();
    renderingInfo.pDepthAttachment = hasDepthAttachment ? &depthAttachmentInfo : nullptr;
    renderingInfo.pStencilAttachment = hasStencilAttachment ? &stencilAttachmentInfo : nullptr;

    m_api.vkCmdBeginRenderingKHR(m_cmdBuffer, &renderingInfo);

    m_renderPassActive = true;
}

void CommandRecorder::cmdEndRenderPass(const commands::EndRenderPass& cmd)
{
    m_api.vkCmdEndRenderingKHR(m_cmdBuffer);

    m_renderTargetViews.clear();
    m_resolveTargetViews.clear();
    m_depthStencilView = nullptr;

    m_renderPassActive = false;
}

void CommandRecorder::prepareSetRenderState(const commands::SetRenderState& cmd)
{
    const RenderState& state = cmd.state;

    bool updateBindings = !m_preparedRenderStateValid || cmd.bindingData != m_preparedRenderBindingData;

    bool updateVertexBuffers = !m_preparedRenderStateValid || !arraysEqual(
                                                                  state.vertexBufferCount,
                                                                  m_renderState.vertexBufferCount,
                                                                  state.vertexBuffers,
                                                                  m_renderState.vertexBuffers
                                                              );
    bool updateIndexBuffer = !m_preparedRenderStateValid || state.indexFormat != m_renderState.indexFormat ||
                             state.indexBuffer != m_renderState.indexBuffer;

    if (updateBindings)
    {
        m_preparedRenderBindingData = static_cast<BindingDataImpl*>(cmd.bindingData);
        requireBindingStates(m_preparedRenderBindingData);
    }

    if (updateVertexBuffers)
    {
        for (uint32_t i = 0; i < state.vertexBufferCount; ++i)
        {
            BufferImpl* buffer = checked_cast<BufferImpl*>(state.vertexBuffers[i].buffer);
            requireBufferState(buffer, ResourceState::VertexBuffer);
        }
    }

    if (updateIndexBuffer)
    {
        if (state.indexBuffer.buffer)
        {
            BufferImpl* buffer = checked_cast<BufferImpl*>(state.indexBuffer.buffer);
            requireBufferState(buffer, ResourceState::IndexBuffer);
        }
    }

    m_preparedRenderStateValid = true;
    m_preparedRenderState = state;
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

    auto& api = m_device->m_api;

    if (updatePipeline)
    {
        m_renderPipeline = checked_cast<RenderPipelineImpl*>(cmd.pipeline);
        api.vkCmdBindPipeline(m_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_renderPipeline->m_pipeline);
    }

    if (updateBindings)
    {
        m_bindingData = static_cast<BindingDataImpl*>(cmd.bindingData);
        setBindings(m_bindingData, VK_PIPELINE_BIND_POINT_GRAPHICS);
    }

    // TODO support setting sample positions
#if 0
    if (updateSamplePositions)
    {
        if (api.vkCmdSetSampleLocationsEXT)
        {
            VkSampleLocationsInfoEXT sampleLocInfo = {};
            sampleLocInfo.sType = VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT;
            sampleLocInfo.sampleLocationsCount = samplesPerPixel * pixelCount;
            sampleLocInfo.sampleLocationsPerPixel = (VkSampleCountFlagBits)samplesPerPixel;
            api.vkCmdSetSampleLocationsEXT(m_vkCommandBuffer, &sampleLocInfo);
        }
    }
#endif

    if (updateStencilRef)
    {
        api.vkCmdSetStencilReference(m_cmdBuffer, VK_STENCIL_FRONT_AND_BACK, state.stencilRef);
    }

    if (updateVertexBuffers)
    {
        VkBuffer vertexBuffers[SLANG_COUNT_OF(state.vertexBuffers)];
        VkDeviceSize offsets[SLANG_COUNT_OF(state.vertexBuffers)];
        for (uint32_t i = 0; i < state.vertexBufferCount; ++i)
        {
            BufferImpl* buffer = checked_cast<BufferImpl*>(state.vertexBuffers[i].buffer);

            vertexBuffers[i] = buffer->m_buffer.m_buffer;
            offsets[i] = state.vertexBuffers[i].offset;
        }
        if (state.vertexBufferCount > 0)
        {
            api.vkCmdBindVertexBuffers(m_cmdBuffer, 0, state.vertexBufferCount, vertexBuffers, offsets);
        }
    }

    if (updateIndexBuffer)
    {
        if (state.indexBuffer.buffer)
        {
            BufferImpl* buffer = checked_cast<BufferImpl*>(state.indexBuffer.buffer);
            VkDeviceSize offset = state.indexBuffer.offset;
            VkIndexType indexType =
                state.indexFormat == IndexFormat::Uint32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;

            api.vkCmdBindIndexBuffer(m_cmdBuffer, buffer->m_buffer.m_buffer, offset, indexType);
        }
        else
        {
            // api.vkCmdBindIndexBuffer(m_cmdBuffer, VK_NULL_HANDLE, 0, VK_INDEX_TYPE_UINT32);
        }
    }

    if (updateViewports)
    {
        VkViewport viewports[SLANG_COUNT_OF(state.viewports)];
        for (uint32_t i = 0; i < state.viewportCount; ++i)
        {
            const Viewport& src = state.viewports[i];
            VkViewport& dst = viewports[i];
            dst.x = src.originX;
            dst.y = src.originY + src.extentY;
            dst.width = src.extentX;
            dst.height = -src.extentY;
            dst.minDepth = src.minZ;
            dst.maxDepth = src.maxZ;
        }
        api.vkCmdSetViewport(m_cmdBuffer, 0, state.viewportCount, viewports);
    }

    if (updateScissorRects)
    {
        VkRect2D scissorRects[SLANG_COUNT_OF(state.scissorRects)];
        for (uint32_t i = 0; i < state.scissorRectCount; ++i)
        {
            const ScissorRect& src = state.scissorRects[i];
            VkRect2D& dst = scissorRects[i];
            dst.offset.x = src.minX;
            dst.offset.y = src.minY;
            dst.extent.width = src.maxX - src.minX;
            dst.extent.height = src.maxY - src.minY;
        }
        api.vkCmdSetScissor(m_cmdBuffer, 0, state.scissorRectCount, scissorRects);
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

    m_api.vkCmdDraw(
        m_cmdBuffer,
        cmd.args.vertexCount,
        cmd.args.instanceCount,
        cmd.args.startVertexLocation,
        cmd.args.startInstanceLocation
    );
}

void CommandRecorder::cmdDrawIndexed(const commands::DrawIndexed& cmd)
{
    if (!m_renderStateValid)
        return;

    m_api.vkCmdDrawIndexed(
        m_cmdBuffer,
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

    auto argBuffer = checked_cast<BufferImpl*>(cmd.argBuffer.buffer);
    auto countBuffer = checked_cast<BufferImpl*>(cmd.countBuffer.buffer);

    requireBufferState(argBuffer, ResourceState::IndirectArgument);
    if (countBuffer)
    {
        requireBufferState(countBuffer, ResourceState::IndirectArgument);
    }
    commitBarriers();

    if (countBuffer)
    {
        m_api.vkCmdDrawIndirectCount(
            m_cmdBuffer,
            argBuffer->m_buffer.m_buffer,
            cmd.argBuffer.offset,
            countBuffer->m_buffer.m_buffer,
            cmd.argBuffer.offset,
            cmd.maxDrawCount,
            sizeof(VkDrawIndirectCommand)
        );
    }
    else
    {
        m_api.vkCmdDrawIndirect(
            m_cmdBuffer,
            argBuffer->m_buffer.m_buffer,
            cmd.argBuffer.offset,
            cmd.maxDrawCount,
            sizeof(VkDrawIndirectCommand)
        );
    }
}

void CommandRecorder::cmdDrawIndexedIndirect(const commands::DrawIndexedIndirect& cmd)
{
    if (!m_renderStateValid)
        return;

    auto argBuffer = checked_cast<BufferImpl*>(cmd.argBuffer.buffer);
    auto countBuffer = checked_cast<BufferImpl*>(cmd.countBuffer.buffer);

    requireBufferState(argBuffer, ResourceState::IndirectArgument);
    if (countBuffer)
    {
        requireBufferState(countBuffer, ResourceState::IndirectArgument);
    }
    commitBarriers();

    if (countBuffer)
    {
        m_api.vkCmdDrawIndexedIndirectCount(
            m_cmdBuffer,
            argBuffer->m_buffer.m_buffer,
            cmd.argBuffer.offset,
            countBuffer->m_buffer.m_buffer,
            cmd.countBuffer.offset,
            cmd.maxDrawCount,
            sizeof(VkDrawIndexedIndirectCommand)
        );
    }
    else
    {
        m_api.vkCmdDrawIndexedIndirect(
            m_cmdBuffer,
            argBuffer->m_buffer.m_buffer,
            cmd.argBuffer.offset,
            cmd.maxDrawCount,
            sizeof(VkDrawIndexedIndirectCommand)
        );
    }
}

void CommandRecorder::cmdDrawMeshTasks(const commands::DrawMeshTasks& cmd)
{
    if (!m_renderStateValid)
        return;

    m_api.vkCmdDrawMeshTasksEXT(m_cmdBuffer, cmd.x, cmd.y, cmd.z);
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

    auto& api = m_device->m_api;

    if (updatePipeline)
    {
        m_computePipeline = checked_cast<ComputePipelineImpl*>(cmd.pipeline);
        api.vkCmdBindPipeline(m_cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline->m_pipeline);
    }

    if (updateBindings)
    {
        m_bindingData = static_cast<BindingDataImpl*>(cmd.bindingData);
        requireBindingStates(m_bindingData);
        commitBarriers();
        setBindings(m_bindingData, VK_PIPELINE_BIND_POINT_COMPUTE);
    }

    m_computeStateValid = true;

    m_renderStateValid = false;
    m_preparedRenderStateValid = false;
    m_rayTracingStateValid = false;
}

void CommandRecorder::cmdDispatchCompute(const commands::DispatchCompute& cmd)
{
    if (!m_computeStateValid)
        return;

    m_api.vkCmdDispatch(m_cmdBuffer, cmd.x, cmd.y, cmd.z);
}

void CommandRecorder::cmdDispatchComputeIndirect(const commands::DispatchComputeIndirect& cmd)
{
    if (!m_computeStateValid)
        return;

    auto argBuffer = checked_cast<BufferImpl*>(cmd.argBuffer.buffer);
    requireBufferState(argBuffer, ResourceState::IndirectArgument);
    commitBarriers();

    m_api.vkCmdDispatchIndirect(m_cmdBuffer, argBuffer->m_buffer.m_buffer, cmd.argBuffer.offset);
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
    bool updateShaderTable = !m_rayTracingStateValid || cmd.shaderTable != m_shaderTable;

    auto& api = m_device->m_api;

    if (updatePipeline)
    {
        m_rayTracingPipeline = checked_cast<RayTracingPipelineImpl*>(cmd.pipeline);
        api.vkCmdBindPipeline(m_cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rayTracingPipeline->m_pipeline);
    }

    if (updateBindings)
    {
        m_bindingData = static_cast<BindingDataImpl*>(cmd.bindingData);
        requireBindingStates(m_bindingData);
        commitBarriers();
        setBindings(m_bindingData, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR);
    }

    if (updateShaderTable)
    {
        m_shaderTable = checked_cast<ShaderTableImpl*>(cmd.shaderTable);

        BufferImpl* shaderTableBuffer = m_shaderTable->getBuffer(m_rayTracingPipeline);
        DeviceAddress shaderTableAddr = shaderTableBuffer->getDeviceAddress();

        auto rtpProps = api.m_rayTracingPipelineProperties;
        size_t alignedHandleSize =
            math::calcAligned2(rtpProps.shaderGroupHandleSize, rtpProps.shaderGroupHandleAlignment);

        // Raygen index is set at dispatch time.
        m_rayGenTableAddr = shaderTableAddr;
        m_raygenSBT.stride = math::calcAligned2(alignedHandleSize, rtpProps.shaderGroupBaseAlignment);
        m_raygenSBT.deviceAddress = shaderTableAddr;
        m_raygenSBT.size = m_raygenSBT.stride;

        m_missSBT.deviceAddress = shaderTableAddr + m_shaderTable->m_raygenTableSize;
        m_missSBT.stride = alignedHandleSize;
        m_missSBT.size = m_shaderTable->m_missTableSize;

        m_hitSBT.deviceAddress = m_missSBT.deviceAddress + m_missSBT.size;
        m_hitSBT.stride = alignedHandleSize;
        m_hitSBT.size = m_shaderTable->m_hitTableSize;

        m_callableSBT.deviceAddress = m_hitSBT.deviceAddress + m_hitSBT.size;
        m_callableSBT.stride = alignedHandleSize;
        m_callableSBT.size = m_shaderTable->m_callableTableSize;
    }

    m_rayTracingStateValid = true;

    m_computeStateValid = false;
    m_renderStateValid = false;
    m_preparedRenderStateValid = false;
}

void CommandRecorder::cmdDispatchRays(const commands::DispatchRays& cmd)
{
    if (!m_rayTracingStateValid)
        return;

    m_raygenSBT.deviceAddress = m_rayGenTableAddr + cmd.rayGenShaderIndex * m_raygenSBT.stride;

    m_api.vkCmdTraceRaysKHR(
        m_cmdBuffer,
        &m_raygenSBT,
        &m_missSBT,
        &m_hitSBT,
        &m_callableSBT,
        cmd.width,
        cmd.height,
        cmd.depth
    );
}

void CommandRecorder::cmdBuildAccelerationStructure(const commands::BuildAccelerationStructure& cmd)
{
    AccelerationStructureBuildDescConverter converter;
    if (converter.convert(cmd.desc, m_device->m_debugCallback) != SLANG_OK)
        return;

    converter.buildInfo.dstAccelerationStructure = checked_cast<AccelerationStructureImpl*>(cmd.dst)->m_vkHandle;
    if (cmd.src)
    {
        converter.buildInfo.srcAccelerationStructure = checked_cast<AccelerationStructureImpl*>(cmd.src)->m_vkHandle;
    }
    converter.buildInfo.scratchData.deviceAddress = cmd.scratchBuffer.getDeviceAddress();

    std::vector<VkAccelerationStructureBuildRangeInfoKHR> rangeInfos;
    rangeInfos.resize(converter.primitiveCounts.size());
    for (size_t i = 0; i < converter.primitiveCounts.size(); i++)
    {
        auto& rangeInfo = rangeInfos[i];
        rangeInfo.primitiveCount = converter.primitiveCounts[i];
        rangeInfo.firstVertex = 0;
        rangeInfo.primitiveOffset = 0;
        rangeInfo.transformOffset = 0;
    }

    auto rangeInfoPtr = rangeInfos.data();
    m_api.vkCmdBuildAccelerationStructuresKHR(m_cmdBuffer, 1, &converter.buildInfo, &rangeInfoPtr);

    if (cmd.propertyQueryCount)
    {
        accelerationStructureBarrier(1, &cmd.dst, AccessFlag::Write, AccessFlag::Read);
        queryAccelerationStructureProperties(1, &cmd.dst, cmd.propertyQueryCount, cmd.queryDescs);
    }
}

void CommandRecorder::cmdCopyAccelerationStructure(const commands::CopyAccelerationStructure& cmd)
{
    VkCopyAccelerationStructureInfoKHR copyInfo = {VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
    copyInfo.src = checked_cast<AccelerationStructureImpl*>(cmd.src)->m_vkHandle;
    copyInfo.dst = checked_cast<AccelerationStructureImpl*>(cmd.dst)->m_vkHandle;
    switch (cmd.mode)
    {
    case AccelerationStructureCopyMode::Clone:
        copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR;
        break;
    case AccelerationStructureCopyMode::Compact:
        copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
        break;
    }
    m_api.vkCmdCopyAccelerationStructureKHR(m_cmdBuffer, &copyInfo);
}

void CommandRecorder::cmdQueryAccelerationStructureProperties(const commands::QueryAccelerationStructureProperties& cmd)
{
    queryAccelerationStructureProperties(
        cmd.accelerationStructureCount,
        cmd.accelerationStructures,
        cmd.queryCount,
        cmd.queryDescs
    );
}

void CommandRecorder::cmdSerializeAccelerationStructure(const commands::SerializeAccelerationStructure& cmd)
{
    VkCopyAccelerationStructureToMemoryInfoKHR copyInfo = {
        VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_TO_MEMORY_INFO_KHR
    };
    copyInfo.src = checked_cast<AccelerationStructureImpl*>(cmd.src)->m_vkHandle;
    copyInfo.dst.deviceAddress = cmd.dst.getDeviceAddress();
    copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_SERIALIZE_KHR;
    m_api.vkCmdCopyAccelerationStructureToMemoryKHR(m_cmdBuffer, &copyInfo);
}

void CommandRecorder::cmdDeserializeAccelerationStructure(const commands::DeserializeAccelerationStructure& cmd)
{
    VkCopyMemoryToAccelerationStructureInfoKHR copyInfo = {
        VK_STRUCTURE_TYPE_COPY_MEMORY_TO_ACCELERATION_STRUCTURE_INFO_KHR
    };
    copyInfo.src.deviceAddress = cmd.src.getDeviceAddress();
    copyInfo.dst = checked_cast<AccelerationStructureImpl*>(cmd.dst)->m_vkHandle;
    copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_DESERIALIZE_KHR;
    m_api.vkCmdCopyMemoryToAccelerationStructureKHR(m_cmdBuffer, &copyInfo);
}

void CommandRecorder::cmdConvertCooperativeVectorMatrix(const commands::ConvertCooperativeVectorMatrix& cmd)
{
    BufferImpl* dstBuffer = checked_cast<BufferImpl*>(cmd.dstBuffer);
    BufferImpl* srcBuffer = checked_cast<BufferImpl*>(cmd.srcBuffer);

    requireBufferState(dstBuffer, ResourceState::UnorderedAccess);
    requireBufferState(srcBuffer, ResourceState::ShaderResource);
    commitBarriers();

    short_vector<VkConvertCooperativeVectorMatrixInfoNV> infos;
    for (uint32_t i = 0; i < cmd.matrixCount; ++i)
    {
        const CooperativeVectorMatrixDesc& dstDesc = cmd.dstDescs[i];
        const CooperativeVectorMatrixDesc& srcDesc = cmd.srcDescs[i];
        VkConvertCooperativeVectorMatrixInfoNV info = {VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV};
        info.srcSize = srcDesc.size;
        info.srcData.deviceAddress = srcBuffer->getDeviceAddress() + srcDesc.offset;
        info.pDstSize = (size_t*)&dstDesc.size;
        info.dstData.deviceAddress = dstBuffer->getDeviceAddress() + dstDesc.offset;
        info.srcComponentType = translateCooperativeVectorComponentType(srcDesc.componentType);
        info.dstComponentType = translateCooperativeVectorComponentType(dstDesc.componentType);
        info.numRows = srcDesc.rowCount;
        info.numColumns = srcDesc.colCount;
        info.srcLayout = translateCooperativeVectorMatrixLayout(srcDesc.layout);
        info.srcStride = srcDesc.rowColumnStride;
        info.dstLayout = translateCooperativeVectorMatrixLayout(dstDesc.layout);
        info.dstStride = dstDesc.rowColumnStride;
        infos.push_back(info);
    }
    m_api.vkCmdConvertCooperativeVectorMatrixNV(m_cmdBuffer, infos.size(), infos.data());

    requireBufferState(dstBuffer, ResourceState::ShaderResource);
    commitBarriers();
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
    // On vulkan the global barrier is a memory barrier that:
    // - captures all stages both before and after the barrier
    // - ensures that all reads after the barrier see all writes before the barrier

    VkMemoryBarrier memoryBarrier = {};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    m_api.vkCmdPipelineBarrier(
        m_cmdBuffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VkDependencyFlags(0),
        1,
        &memoryBarrier,
        0,
        nullptr,
        0,
        nullptr
    );
}

void CommandRecorder::cmdPushDebugGroup(const commands::PushDebugGroup& cmd)
{
    if (!m_api.vkCmdBeginDebugUtilsLabelEXT)
        return;

    VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    label.pLabelName = cmd.name;
    label.color[0] = cmd.color.r;
    label.color[1] = cmd.color.g;
    label.color[2] = cmd.color.b;
    label.color[3] = 1.0f;
    m_api.vkCmdBeginDebugUtilsLabelEXT(m_cmdBuffer, &label);
}

void CommandRecorder::cmdPopDebugGroup(const commands::PopDebugGroup& cmd)
{
    if (!m_api.vkCmdEndDebugUtilsLabelEXT)
        return;

    m_api.vkCmdEndDebugUtilsLabelEXT(m_cmdBuffer);
}

void CommandRecorder::cmdInsertDebugMarker(const commands::InsertDebugMarker& cmd)
{
    SLANG_UNUSED(cmd);
    if (!m_api.vkCmdInsertDebugUtilsLabelEXT)
        return;

    VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    label.pLabelName = cmd.name;
    label.color[0] = cmd.color.r;
    label.color[1] = cmd.color.g;
    label.color[2] = cmd.color.b;
    label.color[3] = 1.0f;
    m_api.vkCmdInsertDebugUtilsLabelEXT(m_cmdBuffer, &label);
}

void CommandRecorder::cmdWriteTimestamp(const commands::WriteTimestamp& cmd)
{
    auto queryPool = checked_cast<QueryPoolImpl*>(cmd.queryPool);
    uint32_t queryIndex = (uint32_t)cmd.queryIndex;
    m_api.vkCmdResetQueryPool(m_cmdBuffer, queryPool->m_pool, queryIndex, 1);
    m_api.vkCmdWriteTimestamp(m_cmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool->m_pool, queryIndex);
}

void CommandRecorder::cmdExecuteCallback(const commands::ExecuteCallback& cmd)
{
    cmd.callback(cmd.userData);
}

void CommandRecorder::setBindings(BindingDataImpl* bindingData, VkPipelineBindPoint bindPoint)
{
    // Set push constants.
    for (uint32_t i = 0; i < bindingData->pushConstantCount; ++i)
    {
        VkPushConstantRange range = bindingData->pushConstantRanges[i];
        void* data = bindingData->pushConstantData[i];
        m_api.vkCmdPushConstants(
            m_cmdBuffer,
            bindingData->pipelineLayout,
            range.stageFlags,
            range.offset,
            range.size,
            data
        );
    }

    // Bind descriptor sets.
    if (bindingData->descriptorSetCount)
    {
        m_api.vkCmdBindDescriptorSets(
            m_cmdBuffer,
            bindPoint,
            bindingData->pipelineLayout,
            0,
            bindingData->descriptorSetCount,
            bindingData->descriptorSets,
            0,
            nullptr
        );
    }
}

void CommandRecorder::requireBindingStates(BindingDataImpl* bindingData)
{
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

    short_vector<VkBufferMemoryBarrier, 16> bufferBarriers;
    short_vector<VkImageMemoryBarrier, 16> imageBarriers;

    VkPipelineStageFlags activeBeforeStageFlags = VkPipelineStageFlags(0);
    VkPipelineStageFlags activeAfterStageFlags = VkPipelineStageFlags(0);

    auto submitBufferBarriers = [&]()
    {
        m_api.vkCmdPipelineBarrier(
            m_cmdBuffer,
            activeBeforeStageFlags,
            activeAfterStageFlags,
            VkDependencyFlags(0),
            0,
            nullptr,
            (uint32_t)bufferBarriers.size(),
            bufferBarriers.data(),
            0,
            nullptr
        );
    };

    auto submitImageBarriers = [&]()
    {
        m_api.vkCmdPipelineBarrier(
            m_cmdBuffer,
            activeBeforeStageFlags,
            activeAfterStageFlags,
            VkDependencyFlags(0),
            0,
            nullptr,
            0,
            nullptr,
            (uint32_t)imageBarriers.size(),
            imageBarriers.data()
        );
    };

    for (const auto& bufferBarrier : m_stateTracking.getBufferBarriers())
    {
        BufferImpl* buffer = checked_cast<BufferImpl*>(bufferBarrier.buffer);

        VkPipelineStageFlags beforeStageFlags = calcPipelineStageFlags(bufferBarrier.stateBefore, true);
        VkPipelineStageFlags afterStageFlags = calcPipelineStageFlags(bufferBarrier.stateAfter, false);

        if ((beforeStageFlags != activeBeforeStageFlags || afterStageFlags != activeAfterStageFlags) &&
            !bufferBarriers.empty())
        {
            submitBufferBarriers();
            bufferBarriers.clear();
        }

        activeBeforeStageFlags = beforeStageFlags;
        activeAfterStageFlags = afterStageFlags;

        VkBufferMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = calcAccessFlags(bufferBarrier.stateBefore);
        barrier.dstAccessMask = calcAccessFlags(bufferBarrier.stateAfter);
        barrier.buffer = buffer->m_buffer.m_buffer;
        barrier.offset = 0;
        barrier.size = buffer->m_desc.size;

        bufferBarriers.push_back(barrier);
    }
    if (!bufferBarriers.empty())
    {
        submitBufferBarriers();
    }

    activeBeforeStageFlags = VkPipelineStageFlags(0);
    activeAfterStageFlags = VkPipelineStageFlags(0);

    for (const auto& textureBarrier : m_stateTracking.getTextureBarriers())
    {
        TextureImpl* texture = checked_cast<TextureImpl*>(textureBarrier.texture);

        VkPipelineStageFlags beforeStageFlags = calcPipelineStageFlags(textureBarrier.stateBefore, true);
        VkPipelineStageFlags afterStageFlags = calcPipelineStageFlags(textureBarrier.stateAfter, false);

        if ((beforeStageFlags != activeBeforeStageFlags || afterStageFlags != activeAfterStageFlags) &&
            !imageBarriers.empty())
        {
            submitImageBarriers();
            imageBarriers.clear();
        }

        activeBeforeStageFlags = beforeStageFlags;
        activeAfterStageFlags = afterStageFlags;

        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = texture->m_image;
        barrier.oldLayout = translateImageLayout(textureBarrier.stateBefore);
        // This is a bit of a hack.
        // When we first transition a swapchain image, it starts in VK_IMAGE_LAYOUT_UNDEFINED.
        // The default state for swapchain images (automatically transitioned to at the end of a command encoding is
        // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR).
        if (texture->m_isSwapchainInitialState)
        {
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            texture->m_isSwapchainInitialState = false;
        }
        barrier.newLayout = translateImageLayout(textureBarrier.stateAfter);
        barrier.subresourceRange.aspectMask = getAspectMaskFromFormat(getVkFormat(texture->m_desc.format));
        barrier.subresourceRange.baseArrayLayer = textureBarrier.entireTexture ? 0 : textureBarrier.layer;
        barrier.subresourceRange.baseMipLevel = textureBarrier.entireTexture ? 0 : textureBarrier.mip;
        barrier.subresourceRange.layerCount = textureBarrier.entireTexture ? VK_REMAINING_ARRAY_LAYERS : 1;
        barrier.subresourceRange.levelCount = textureBarrier.entireTexture ? VK_REMAINING_MIP_LEVELS : 1;
        barrier.srcAccessMask = calcAccessFlags(textureBarrier.stateBefore);
        barrier.dstAccessMask = calcAccessFlags(textureBarrier.stateAfter);
        imageBarriers.push_back(barrier);
    }
    if (!imageBarriers.empty())
    {
        submitImageBarriers();
    }

    m_stateTracking.clearBarriers();
}

void CommandRecorder::queryAccelerationStructureProperties(
    uint32_t accelerationStructureCount,
    IAccelerationStructure* const* accelerationStructures,
    uint32_t queryCount,
    const AccelerationStructureQueryDesc* queryDescs
)
{
    short_vector<VkAccelerationStructureKHR> vkHandles;
    vkHandles.resize(accelerationStructureCount);
    for (uint32_t i = 0; i < accelerationStructureCount; i++)
    {
        vkHandles[i] = checked_cast<AccelerationStructureImpl*>(accelerationStructures[i])->m_vkHandle;
    }
    for (uint32_t i = 0; i < queryCount; i++)
    {
        VkQueryType queryType;
        switch (queryDescs[i].queryType)
        {
        case QueryType::AccelerationStructureCompactedSize:
            queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
            break;
        case QueryType::AccelerationStructureSerializedSize:
            queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR;
            break;
        case QueryType::AccelerationStructureCurrentSize:
            continue;
        default:
            m_device->handleMessage(
                DebugMessageType::Error,
                DebugMessageSource::Layer,
                "Invalid query type for use in queryAccelerationStructureProperties."
            );
            return;
        }
        auto queryPool = checked_cast<QueryPoolImpl*>(queryDescs[i].queryPool)->m_pool;
        m_device->m_api.vkCmdResetQueryPool(m_cmdBuffer, queryPool, (uint32_t)queryDescs[i].firstQueryIndex, 1);
        m_device->m_api.vkCmdWriteAccelerationStructuresPropertiesKHR(
            m_cmdBuffer,
            accelerationStructureCount,
            vkHandles.data(),
            queryType,
            queryPool,
            queryDescs[i].firstQueryIndex
        );
    }
}

void CommandRecorder::accelerationStructureBarrier(
    uint32_t accelerationStructureCount,
    IAccelerationStructure* const* accelerationStructures,
    AccessFlag srcAccess,
    AccessFlag destAccess
)
{
    short_vector<VkBufferMemoryBarrier> memBarriers;
    memBarriers.resize(accelerationStructureCount);
    for (uint32_t i = 0; i < accelerationStructureCount; i++)
    {
        memBarriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        memBarriers[i].pNext = nullptr;
        memBarriers[i].dstAccessMask = translateAccelerationStructureAccessFlag(destAccess);
        memBarriers[i].srcAccessMask = translateAccelerationStructureAccessFlag(srcAccess);
        memBarriers[i].srcQueueFamilyIndex = m_device->m_queueFamilyIndex;
        memBarriers[i].dstQueueFamilyIndex = m_device->m_queueFamilyIndex;

        auto asImpl = checked_cast<AccelerationStructureImpl*>(accelerationStructures[i]);
        memBarriers[i].buffer = asImpl->m_buffer->m_buffer.m_buffer;
        memBarriers[i].offset = 0;
        memBarriers[i].size = asImpl->m_buffer->m_desc.size;
    }

    VkPipelineStageFlagBits dstStageMask = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    if (m_device->m_api.m_extendedFeatures.rayQueryFeatures.rayQuery)
    {
        // for VUID-vkCmdPipelineBarrier-dstAccessMask-06257
        // If the rayQuery feature is not enabled and a memory barrier dstAccessMask includes
        // VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR, dstStageMask must not include any of the
        // VK_PIPELINE_STAGE_*_SHADER_BIT stages except VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR
        dstStageMask =
            (VkPipelineStageFlagBits)(VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
                                      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                      VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
    }
    m_device->m_api.vkCmdPipelineBarrier(
        m_cmdBuffer,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        dstStageMask,
        0,
        0,
        nullptr,
        (uint32_t)memBarriers.size(),
        memBarriers.data(),
        0,
        nullptr
    );
}


// CommandQueueImpl

CommandQueueImpl::CommandQueueImpl(Device* device, QueueType type)
    : CommandQueue(device, type)
    , m_api(getDevice<DeviceImpl>()->m_api)
{
}

CommandQueueImpl::~CommandQueueImpl()
{
    m_api.vkQueueWaitIdle(m_queue);
    m_api.vkDestroySemaphore(m_api.m_device, m_trackingSemaphore, nullptr);
}

void CommandQueueImpl::init(VkQueue queue, uint32_t queueFamilyIndex)
{
    m_queue = queue;
    m_queueFamilyIndex = queueFamilyIndex;

    {
        VkSemaphoreTypeCreateInfo timelineCreateInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo semaphoreCreateInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        semaphoreCreateInfo.pNext = &timelineCreateInfo;
        m_api.vkCreateSemaphore(m_api.m_device, &semaphoreCreateInfo, nullptr, &m_trackingSemaphore);
    }
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
    m_api.vkGetSemaphoreCounterValue(m_api.m_device, m_trackingSemaphore, &m_lastFinishedID);
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

    // Collect & process command buffers.
    short_vector<VkCommandBuffer> vkCommandBuffers;
    for (uint32_t i = 0; i < desc.commandBufferCount; i++)
    {
        CommandBufferImpl* commandBuffer = checked_cast<CommandBufferImpl*>(desc.commandBuffers[i]);
        commandBuffer->m_submissionID = m_lastSubmittedID;
        m_commandBuffersInFlight.push_back(commandBuffer);
        vkCommandBuffers.push_back(commandBuffer->m_commandBuffer);
    }

    // Setup wait semaphores.
    short_vector<VkSemaphore> waitSemaphores;
    short_vector<uint64_t> waitValues;
    short_vector<VkPipelineStageFlags> waitStages;
    auto addWaitSemaphore = [&waitSemaphores, &waitValues, &waitStages](
                                VkSemaphore semaphore,
                                uint64_t value,
                                VkPipelineStageFlags stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
                            )
    {
        waitSemaphores.push_back(semaphore);
        waitValues.push_back(value);
        waitStages.push_back(stage);
    };

    if (m_surfaceSync.imageAvailableSemaphore != VK_NULL_HANDLE)
    {
        addWaitSemaphore(m_surfaceSync.imageAvailableSemaphore, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        m_surfaceSync.imageAvailableSemaphore = VK_NULL_HANDLE;
    }

    for (uint32_t i = 0; i < desc.waitFenceCount; ++i)
    {
        FenceImpl* fence = checked_cast<FenceImpl*>(desc.waitFences[i]);
        addWaitSemaphore(fence->m_semaphore, desc.waitFenceValues[i]);
    }

    // Setup signal semaphores.
    short_vector<VkSemaphore> signalSemaphores;
    short_vector<uint64_t> signalValues;
    auto addSignalSemaphore = [&signalSemaphores, &signalValues](VkSemaphore semaphore, uint64_t value)
    {
        signalSemaphores.push_back(semaphore);
        signalValues.push_back(value);
    };

    if (m_surfaceSync.renderFinishedSemaphore != VK_NULL_HANDLE)
    {
        addSignalSemaphore(m_surfaceSync.renderFinishedSemaphore, 0);
        m_surfaceSync.renderFinishedSemaphore = VK_NULL_HANDLE;
    }

    addSignalSemaphore(m_trackingSemaphore, m_lastSubmittedID);
    for (uint32_t i = 0; i < desc.signalFenceCount; ++i)
    {
        FenceImpl* fence = checked_cast<FenceImpl*>(desc.signalFences[i]);
        addSignalSemaphore(fence->m_semaphore, desc.signalFenceValues[i]);
    }

    // Setup submit info.
    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = vkCommandBuffers.size();
    submitInfo.pCommandBuffers = vkCommandBuffers.data();

    VkTimelineSemaphoreSubmitInfo timelineSubmitInfo = {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    submitInfo.pNext = &timelineSubmitInfo;

    if (waitSemaphores.size() > 0)
    {
        submitInfo.waitSemaphoreCount = (uint32_t)waitSemaphores.size();
        submitInfo.pWaitSemaphores = waitSemaphores.data();
        submitInfo.pWaitDstStageMask = waitStages.data();
        timelineSubmitInfo.waitSemaphoreValueCount = (uint32_t)waitValues.size();
        timelineSubmitInfo.pWaitSemaphoreValues = waitValues.data();
    }

    if (signalSemaphores.size() > 0)
    {
        submitInfo.signalSemaphoreCount = (uint32_t)signalSemaphores.size();
        submitInfo.pSignalSemaphores = signalSemaphores.data();
        timelineSubmitInfo.signalSemaphoreValueCount = (uint32_t)signalValues.size();
        timelineSubmitInfo.pSignalSemaphoreValues = signalValues.data();
    }

    SLANG_VK_RETURN_ON_FAIL(m_api.vkQueueSubmit(m_queue, 1, &submitInfo, m_surfaceSync.fence));
    m_surfaceSync.fence = VK_NULL_HANDLE;

    retireCommandBuffers();

    return SLANG_OK;
}

Result CommandQueueImpl::waitOnHost()
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    auto& api = device->m_api;
    api.vkQueueWaitIdle(m_queue);
    retireCommandBuffers();
    return SLANG_OK;
}

Result CommandQueueImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkQueue;
    outHandle->value = (uint64_t)m_queue;
    return SLANG_OK;
}

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
    builder.m_descriptorSetAllocator = &m_commandBuffer->m_descriptorSetAllocator;
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
    m_commandBuffer->m_constantBufferPool.finish();
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

CommandBufferImpl::~CommandBufferImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    device->m_api.vkFreeCommandBuffers(device->m_api.m_device, m_commandPool, 1, &m_commandBuffer);
    device->m_api.vkDestroyCommandPool(device->m_api.m_device, m_commandPool, nullptr);
    m_descriptorSetAllocator.close();
}

Result CommandBufferImpl::init()
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    m_constantBufferPool.init(device);
    m_descriptorSetAllocator.init(&device->m_api);

    VkCommandPoolCreateInfo createInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    createInfo.queueFamilyIndex = m_queue->m_queueFamilyIndex;
    SLANG_VK_RETURN_ON_FAIL(
        device->m_api.vkCreateCommandPool(device->m_api.m_device, &createInfo, nullptr, &m_commandPool)
    );

    VkCommandBufferAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    SLANG_VK_RETURN_ON_FAIL(
        device->m_api.vkAllocateCommandBuffers(device->m_api.m_device, &allocInfo, &m_commandBuffer)
    );

    return SLANG_OK;
}

Result CommandBufferImpl::reset()
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    m_commandList.reset();
    SLANG_VK_RETURN_ON_FAIL(device->m_api.vkResetCommandPool(device->m_device, m_commandPool, 0));
    m_constantBufferPool.reset();
    m_descriptorSetAllocator.reset();
    m_bindingCache.reset();
    return CommandBuffer::reset();
}

Result CommandBufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkCommandBuffer;
    outHandle->value = (uint64_t)m_commandBuffer;
    return SLANG_OK;
}

} // namespace rhi::vk
