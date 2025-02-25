#include "vk-command.h"
#include "vk-device.h"
#include "vk-buffer.h"
#include "vk-texture.h"
#include "vk-fence.h"
#include "vk-query.h"
#include "vk-acceleration-structure.h"
#include "vk-shader-table.h"
#include "vk-pipeline.h"
#include "vk-util.h"
#include "vk-helper-functions.h"
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
    void cmdConvertCooperativeVectorMatrix(const commands::ConvertCooperativeVectorMatrix& cmd);
    void cmdSetBufferState(const commands::SetBufferState& cmd);
    void cmdSetTextureState(const commands::SetTextureState& cmd);
    void cmdPushDebugGroup(const commands::PushDebugGroup& cmd);
    void cmdPopDebugGroup(const commands::PopDebugGroup& cmd);
    void cmdInsertDebugMarker(const commands::InsertDebugMarker& cmd);
    void cmdWriteTimestamp(const commands::WriteTimestamp& cmd);
    void cmdExecuteCallback(const commands::ExecuteCallback& cmd);

    void prepareSetRenderState(const commands::SetRenderState& cmd);

    void setBindings(BindingDataImpl* bindingData, VkPipelineBindPoint bindPoint);

    void requireBufferState(BufferImpl* buffer, ResourceState state);
    void requireTextureState(TextureImpl* texture, SubresourceRange subresourceRange, ResourceState state);
    void commitBarriers();

    void queryAccelerationStructureProperties(
        uint32_t accelerationStructureCount,
        IAccelerationStructure* const* accelerationStructures,
        uint32_t queryCount,
        AccelerationStructureQueryDesc* queryDescs
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

#define NOT_SUPPORTED(x) m_device->warning(x " command is not supported!")

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
    Extents extent = cmd.extent;

    requireTextureState(dst, dstSubresource, ResourceState::CopyDestination);
    requireTextureState(src, srcSubresource, ResourceState::CopySource);
    commitBarriers();

    const TextureDesc& srcDesc = src->m_desc;
    VkImageLayout srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const TextureDesc& dstDesc = dst->m_desc;
    VkImageLayout dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    if (dstSubresource.layerCount == 0 && dstSubresource.mipLevelCount == 0)
    {
        extent = dstDesc.size;
        dstSubresource.layerCount = dstDesc.arrayLength * (dstDesc.type == TextureType::TextureCube ? 6 : 1);
        if (dstSubresource.layerCount == 0)
            dstSubresource.layerCount = 1;
        dstSubresource.mipLevelCount = dstDesc.mipLevelCount;
    }
    if (srcSubresource.layerCount == 0 && srcSubresource.mipLevelCount == 0)
    {
        extent = srcDesc.size;
        srcSubresource.layerCount = srcDesc.arrayLength * (dstDesc.type == TextureType::TextureCube ? 6 : 1);
        if (srcSubresource.layerCount == 0)
            srcSubresource.layerCount = 1;
        srcSubresource.mipLevelCount = dstDesc.mipLevelCount;
    }
    VkImageCopy region = {};
    region.srcSubresource.aspectMask = VulkanUtil::getAspectMask(TextureAspect::All, src->m_vkformat);
    region.srcSubresource.baseArrayLayer = srcSubresource.baseArrayLayer;
    region.srcSubresource.mipLevel = srcSubresource.mipLevel;
    region.srcSubresource.layerCount = srcSubresource.layerCount;
    region.srcOffset = {(int32_t)srcOffset.x, (int32_t)srcOffset.y, (int32_t)srcOffset.z};
    region.dstSubresource.aspectMask = VulkanUtil::getAspectMask(TextureAspect::All, dst->m_vkformat);
    region.dstSubresource.baseArrayLayer = dstSubresource.baseArrayLayer;
    region.dstSubresource.mipLevel = dstSubresource.mipLevel;
    region.dstSubresource.layerCount = dstSubresource.layerCount;
    region.dstOffset = {(int32_t)dstOffset.x, (int32_t)dstOffset.y, (int32_t)dstOffset.z};
    region.extent = {(uint32_t)extent.width, (uint32_t)extent.height, (uint32_t)extent.depth};

    m_api.vkCmdCopyImage(m_cmdBuffer, src->m_image, srcImageLayout, dst->m_image, dstImageLayout, 1, &region);
}

void CommandRecorder::cmdCopyTextureToBuffer(const commands::CopyTextureToBuffer& cmd)
{
    SLANG_RHI_ASSERT(cmd.srcSubresource.mipLevelCount <= 1);

    BufferImpl* dst = checked_cast<BufferImpl*>(cmd.dst);
    TextureImpl* src = checked_cast<TextureImpl*>(cmd.src);

    requireBufferState(dst, ResourceState::CopyDestination);
    requireTextureState(src, cmd.srcSubresource, ResourceState::CopySource);
    commitBarriers();

    VkBufferImageCopy region = {};
    region.bufferOffset = cmd.dstOffset;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VulkanUtil::getAspectMask(TextureAspect::All, src->m_vkformat);
    region.imageSubresource.mipLevel = cmd.srcSubresource.mipLevel;
    region.imageSubresource.baseArrayLayer = cmd.srcSubresource.baseArrayLayer;
    region.imageSubresource.layerCount = cmd.srcSubresource.layerCount;
    region.imageOffset = {(int32_t)cmd.srcOffset.x, (int32_t)cmd.srcOffset.y, (int32_t)cmd.srcOffset.z};
    region.imageExtent = {(uint32_t)cmd.extent.width, (uint32_t)cmd.extent.height, (uint32_t)cmd.extent.depth};

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

    m_api.vkCmdFillBuffer(m_cmdBuffer, buffer->m_buffer.m_buffer, cmd.range.offset, cmd.range.size, 0);
}

void CommandRecorder::cmdClearTexture(const commands::ClearTexture& cmd)
{
    TextureImpl* texture = checked_cast<TextureImpl*>(cmd.texture);

    requireTextureState(texture, cmd.subresourceRange, ResourceState::CopyDestination);
    commitBarriers();

    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseArrayLayer = cmd.subresourceRange.baseArrayLayer;
    subresourceRange.baseMipLevel = cmd.subresourceRange.mipLevel;
    subresourceRange.layerCount = cmd.subresourceRange.layerCount;
    subresourceRange.levelCount = 1;

    if (isDepthFormat(texture->m_desc.format))
    {
        VkClearDepthStencilValue vkClearValue = {};
        vkClearValue.depth = cmd.clearValue.depthStencil.depth;
        vkClearValue.stencil = cmd.clearValue.depthStencil.stencil;

        subresourceRange.aspectMask = 0;
        if (cmd.clearDepth)
            subresourceRange.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (cmd.clearStencil)
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
    else
    {
        VkClearColorValue vkClearColor = {};
        std::memcpy(vkClearColor.float32, cmd.clearValue.color.floatValues, sizeof(float) * 4);

        m_api.vkCmdClearColorImage(
            m_cmdBuffer,
            texture->m_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &vkClearColor,
            1,
            &subresourceRange
        );
    }
}

void CommandRecorder::cmdUploadTextureData(const commands::UploadTextureData& cmd)
{
    m_device->warning("uploadTextureData command not implemented");
}

void CommandRecorder::cmdUploadBufferData(const commands::UploadBufferData& cmd)
{
    m_device->warning("uploadBufferData command not implemented");
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
        uint32_t width = getMipLevelSize(viewDesc.subresourceRange.mipLevel, textureDesc.size.width);
        uint32_t height = getMipLevelSize(viewDesc.subresourceRange.mipLevel, textureDesc.size.height);
        renderArea.extent.width = min(renderArea.extent.width, width);
        renderArea.extent.height = min(renderArea.extent.height, height);
        uint32_t attachmentLayerCount = (textureDesc.type == TextureType::Texture3D)
                                            ? textureDesc.size.depth
                                            : viewDesc.subresourceRange.layerCount;
        layerCount = max(layerCount, attachmentLayerCount);

        // Create attachment info
        VkRenderingAttachmentInfoKHR attachmentInfo = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
        attachmentInfo.imageView = checked_cast<TextureViewImpl*>(attachment.view)->getView().imageView;
        attachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        if (attachment.resolveTarget)
        {
            attachmentInfo.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            attachmentInfo.resolveImageView = resolveView->getView().imageView;
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
        uint32_t width = getMipLevelSize(viewDesc.subresourceRange.mipLevel, textureDesc.size.width);
        uint32_t height = getMipLevelSize(viewDesc.subresourceRange.mipLevel, textureDesc.size.height);
        renderArea.extent.width = min(renderArea.extent.width, width);
        renderArea.extent.height = min(renderArea.extent.height, height);

        // Create attachment info
        if (VulkanUtil::isDepthFormat(view->m_texture->m_vkformat))
        {
            hasDepthAttachment = true;
            const auto& dsa = *desc.depthStencilAttachment;
            depthAttachmentInfo.imageView = checked_cast<TextureViewImpl*>(dsa.view)->getView().imageView;
            depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachmentInfo.loadOp = translateLoadOp(dsa.depthLoadOp);
            depthAttachmentInfo.storeOp = translateStoreOp(dsa.depthStoreOp);
            depthAttachmentInfo.clearValue.depthStencil.depth = dsa.depthClearValue;
        }
        if (VulkanUtil::isStencilFormat(view->m_texture->m_vkformat))
        {
            hasStencilAttachment = true;
            const auto& dsa = *desc.depthStencilAttachment;
            stencilAttachmentInfo.imageView = checked_cast<TextureViewImpl*>(dsa.view)->getView().imageView;
            stencilAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            stencilAttachmentInfo.loadOp = translateLoadOp(dsa.stencilLoadOp);
            stencilAttachmentInfo.storeOp = translateStoreOp(dsa.stencilStoreOp);
            stencilAttachmentInfo.clearValue.depthStencil.stencil = dsa.stencilClearValue;
        }
    }

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

    bool updateVertexBuffers = !arraysEqual(
        state.vertexBufferCount,
        m_renderState.vertexBufferCount,
        state.vertexBuffers,
        m_renderState.vertexBuffers
    );
    bool updateIndexBuffer =
        state.indexFormat != m_renderState.indexFormat || state.indexBuffer != m_renderState.indexBuffer;

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
                state.indexFormat == IndexFormat::UInt32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;

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
            dst.extent.width = uint32_t(src.maxX - src.minX);
            dst.extent.height = uint32_t(src.maxY - src.minY);
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

    auto argBuffer = checked_cast<BufferImpl*>(cmd.argBuffer);
    auto countBuffer = checked_cast<BufferImpl*>(cmd.countBuffer);

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
            cmd.argOffset,
            countBuffer->m_buffer.m_buffer,
            cmd.countOffset,
            cmd.maxDrawCount,
            sizeof(VkDrawIndirectCommand)
        );
    }
    else
    {
        m_api.vkCmdDrawIndirect(
            m_cmdBuffer,
            argBuffer->m_buffer.m_buffer,
            cmd.argOffset,
            cmd.maxDrawCount,
            sizeof(VkDrawIndirectCommand)
        );
    }
}

void CommandRecorder::cmdDrawIndexedIndirect(const commands::DrawIndexedIndirect& cmd)
{
    if (!m_renderStateValid)
        return;

    auto argBuffer = checked_cast<BufferImpl*>(cmd.argBuffer);
    auto countBuffer = checked_cast<BufferImpl*>(cmd.countBuffer);

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
            cmd.argOffset,
            countBuffer->m_buffer.m_buffer,
            cmd.countOffset,
            cmd.maxDrawCount,
            sizeof(VkDrawIndexedIndirectCommand)
        );
    }
    else
    {
        m_api.vkCmdDrawIndexedIndirect(
            m_cmdBuffer,
            argBuffer->m_buffer.m_buffer,
            cmd.argOffset,
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
        setBindings(m_bindingData, VK_PIPELINE_BIND_POINT_COMPUTE);
    }

    m_computeStateValid = true;
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

    auto argBuffer = checked_cast<BufferImpl*>(cmd.argBuffer);
    requireBufferState(argBuffer, ResourceState::IndirectArgument);
    commitBarriers();

    m_api.vkCmdDispatchIndirect(m_cmdBuffer, argBuffer->m_buffer.m_buffer, cmd.offset);
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
        setBindings(m_bindingData, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR);
    }

    if (updateShaderTable)
    {
        m_shaderTable = checked_cast<ShaderTableImpl*>(cmd.shaderTable);

        BufferImpl* shaderTableBuffer = m_shaderTable->getBuffer(m_rayTracingPipeline);
        DeviceAddress shaderTableAddr = shaderTableBuffer->getDeviceAddress();

        auto rtProps = api.m_rtProperties;
        size_t alignedHandleSize = math::calcAligned(rtProps.shaderGroupHandleSize, rtProps.shaderGroupHandleAlignment);

        // Raygen index is set at dispatch time.
        m_rayGenTableAddr = shaderTableAddr;
        m_raygenSBT.stride = math::calcAligned(alignedHandleSize, rtProps.shaderGroupBaseAlignment);
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
    AccelerationStructureBuildGeometryInfoBuilder geomInfoBuilder;
    if (geomInfoBuilder.build(cmd.desc, m_device->m_debugCallback) != SLANG_OK)
        return;

    geomInfoBuilder.buildInfo.dstAccelerationStructure = checked_cast<AccelerationStructureImpl*>(cmd.dst)->m_vkHandle;
    if (cmd.src)
    {
        geomInfoBuilder.buildInfo.srcAccelerationStructure =
            checked_cast<AccelerationStructureImpl*>(cmd.src)->m_vkHandle;
    }
    geomInfoBuilder.buildInfo.scratchData.deviceAddress = cmd.scratchBuffer.getDeviceAddress();

    std::vector<VkAccelerationStructureBuildRangeInfoKHR> rangeInfos;
    rangeInfos.resize(geomInfoBuilder.primitiveCounts.size());
    for (size_t i = 0; i < geomInfoBuilder.primitiveCounts.size(); i++)
    {
        auto& rangeInfo = rangeInfos[i];
        rangeInfo.primitiveCount = geomInfoBuilder.primitiveCounts[i];
        rangeInfo.firstVertex = 0;
        rangeInfo.primitiveOffset = 0;
        rangeInfo.transformOffset = 0;
    }

    auto rangeInfoPtr = rangeInfos.data();
    m_api.vkCmdBuildAccelerationStructuresKHR(m_cmdBuffer, 1, &geomInfoBuilder.buildInfo, &rangeInfoPtr);

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
    short_vector<VkConvertCooperativeVectorMatrixInfoNV> infos;
    for (uint32_t i = 0; i < cmd.descCount; ++i)
    {
        infos.push_back(VulkanUtil::translateConvertCooperativeVectorMatrixDesc(cmd.descs[i]));
    }
    m_api.vkCmdConvertCooperativeVectorMatrixNV(m_cmdBuffer, infos.size(), infos.data());
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
    if (!m_api.vkCmdBeginDebugUtilsLabelEXT)
        return;

    VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    label.pLabelName = cmd.name;
    label.color[0] = cmd.rgbColor[0];
    label.color[1] = cmd.rgbColor[1];
    label.color[2] = cmd.rgbColor[2];
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
    label.color[0] = cmd.rgbColor[0];
    label.color[1] = cmd.rgbColor[1];
    label.color[2] = cmd.rgbColor[2];
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

    // Then we set all push constants.
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

    // Finally, we bind all descriptor sets.
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
        barrier.newLayout = translateImageLayout(textureBarrier.stateAfter);
        barrier.subresourceRange.aspectMask = getAspectMaskFromFormat(VulkanUtil::getVkFormat(texture->m_desc.format));
        barrier.subresourceRange.baseArrayLayer = textureBarrier.entireTexture ? 0 : textureBarrier.arrayLayer;
        barrier.subresourceRange.baseMipLevel = textureBarrier.entireTexture ? 0 : textureBarrier.mipLevel;
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
    AccelerationStructureQueryDesc* queryDescs
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
    m_device->m_api.vkCmdPipelineBarrier(
        m_cmdBuffer,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT |
            VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
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

CommandQueueImpl::CommandQueueImpl(DeviceImpl* device, QueueType type)
    : CommandQueue(device, type)
    , m_api(device->m_api)
{
}

CommandQueueImpl::~CommandQueueImpl()
{
    m_api.vkQueueWaitIdle(m_queue);
    m_api.vkDestroySemaphore(m_api.m_device, m_semaphore, nullptr);
    m_api.vkDestroySemaphore(m_api.m_device, m_trackingSemaphore, nullptr);
}

void CommandQueueImpl::init(VkQueue queue, uint32_t queueFamilyIndex)
{
    m_queue = queue;
    m_queueFamilyIndex = queueFamilyIndex;

    {
        VkSemaphoreCreateInfo semaphoreCreateInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        m_api.vkCreateSemaphore(m_api.m_device, &semaphoreCreateInfo, nullptr, &m_semaphore);
    }

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
    m_api.vkGetSemaphoreCounterValue(m_api.m_device, m_trackingSemaphore, &m_lastFinishedID);
    return m_lastFinishedID;
}

Result CommandQueueImpl::createCommandEncoder(ICommandEncoder** outEncoder)
{
    RefPtr<CommandEncoderImpl> encoder = new CommandEncoderImpl(m_device, this);
    encoder->init();
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
    auto addWaitSemaphore = [&waitSemaphores, &waitValues](VkSemaphore semaphore, uint64_t value)
    {
        waitSemaphores.push_back(semaphore);
        waitValues.push_back(value);
    };

    for (VkSemaphore s : m_pendingWaitSemaphores)
    {
        if (s != VK_NULL_HANDLE)
        {
            addWaitSemaphore(s, 0);
        }
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
    addSignalSemaphore(m_semaphore, 0);
    addSignalSemaphore(m_trackingSemaphore, m_lastSubmittedID);
    for (uint32_t i = 0; i < desc.signalFenceCount; ++i)
    {
        FenceImpl* fence = checked_cast<FenceImpl*>(desc.signalFences[i]);
        addSignalSemaphore(fence->m_semaphore, desc.signalFenceValues[i]);
    }

    // Setup submit info.
    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkPipelineStageFlags stageFlag[] = {VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT};
    submitInfo.pWaitDstStageMask = stageFlag;
    submitInfo.commandBufferCount = vkCommandBuffers.size();
    submitInfo.pCommandBuffers = vkCommandBuffers.data();

    VkTimelineSemaphoreSubmitInfo timelineSubmitInfo = {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    submitInfo.pNext = &timelineSubmitInfo;

    if (waitSemaphores.size() > 0)
    {
        submitInfo.waitSemaphoreCount = (uint32_t)waitSemaphores.size();
        submitInfo.pWaitSemaphores = waitSemaphores.data();
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

    SLANG_VK_RETURN_ON_FAIL(m_api.vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE));

    m_pendingWaitSemaphores[0] = m_semaphore;
    m_pendingWaitSemaphores[1] = VK_NULL_HANDLE;

    retireCommandBuffers();

    return SLANG_OK;
}

Result CommandQueueImpl::waitOnHost()
{
    auto& api = m_device->m_api;
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
    m_commandList = &m_commandBuffer->m_commandList;
    return SLANG_OK;
}

Device* CommandEncoderImpl::getDevice()
{
    return m_device;
}

Result CommandEncoderImpl::getBindingData(RootShaderObject* rootObject, BindingData*& outBindingData)
{
    rootObject->trackResources(m_commandBuffer->m_trackedObjects);
    BindingDataBuilder builder;
    builder.m_device = m_device;
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

void CommandEncoderImpl::uploadTextureData(
    ITexture* dst,
    SubresourceRange subresourceRange,
    Offset3D offset,
    Extents extent,
    SubresourceData* subresourceData,
    uint32_t subresourceDataCount
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
    m_commandBuffer->m_constantBufferPool.finish();
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

CommandBufferImpl::~CommandBufferImpl()
{
    m_device->m_api.vkFreeCommandBuffers(m_device->m_api.m_device, m_commandPool, 1, &m_commandBuffer);
    m_device->m_api.vkDestroyCommandPool(m_device->m_api.m_device, m_commandPool, nullptr);
    m_descriptorSetAllocator.close();
}

Result CommandBufferImpl::init()
{
    m_constantBufferPool.init(m_device);
    m_descriptorSetAllocator.init(&m_device->m_api);

    VkCommandPoolCreateInfo createInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    createInfo.queueFamilyIndex = m_queue->m_queueFamilyIndex;
    SLANG_VK_RETURN_ON_FAIL(
        m_device->m_api.vkCreateCommandPool(m_device->m_api.m_device, &createInfo, nullptr, &m_commandPool)
    );

    VkCommandBufferAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    SLANG_VK_RETURN_ON_FAIL(
        m_device->m_api.vkAllocateCommandBuffers(m_device->m_api.m_device, &allocInfo, &m_commandBuffer)
    );

    return SLANG_OK;
}

Result CommandBufferImpl::reset()
{
    m_commandList.reset();
    SLANG_VK_RETURN_ON_FAIL(m_device->m_api.vkResetCommandBuffer(m_commandBuffer, 0));
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

// Taken from old code for reference
#if 0
void CommandEncoderImpl::uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data)
{
    BufferImpl* dstImpl = checked_cast<BufferImpl*>(dst);

    requireBufferState(dstImpl, ResourceState::CopyDestination);
    commitBarriers();

    IBuffer* stagingBuffer = nullptr;
    Offset stagingBufferOffset = 0;
    m_transientHeap->allocateStagingBuffer(size, stagingBuffer, stagingBufferOffset, MemoryType::Upload);

    BufferImpl* stagingBufferImpl = checked_cast<BufferImpl*>(stagingBuffer);

    auto& api = m_device->m_api;

    void* mappedData = nullptr;
    if (api.vkMapMemory(
            api.m_device,
            stagingBufferImpl->m_buffer.m_memory,
            0,
            stagingBufferOffset + size,
            0,
            &mappedData
        ) != VK_SUCCESS)
    {
        // TODO issue error message?
        return;
    }
    memcpy((char*)mappedData + stagingBufferOffset, data, size);
    api.vkUnmapMemory(api.m_device, stagingBufferImpl->m_buffer.m_memory);

    // Copy from staging buffer to real buffer
    VkBufferCopy copyInfo = {};
    copyInfo.size = size;
    copyInfo.dstOffset = offset;
    copyInfo.srcOffset = stagingBufferOffset;
    api.vkCmdCopyBuffer(m_cmdBuffer, stagingBufferImpl->m_buffer.m_buffer, dstImpl->m_buffer.m_buffer, 1, &copyInfo);
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

    auto& api = m_device->m_api;
    std::vector<Extents> mipSizes;

    const TextureDesc& desc = dstTexture->m_desc;
    // Calculate how large the buffer has to be
    Size bufferSize = 0;
    // Calculate how large an array entry is
    for (GfxIndex j = subresourceRange.mipLevel; j < subresourceRange.mipLevel + subresourceRange.mipLevelCount; ++j)
    {
        const Extents mipSize = calcMipSize(desc.size, j);

        auto rowSizeInBytes = calcRowSize(desc.format, mipSize.width);
        auto numRows = calcNumRows(desc.format, mipSize.height);

        mipSizes.push_back(mipSize);

        bufferSize += (rowSizeInBytes * numRows) * mipSize.depth;
    }

    // Calculate the total size taking into account the array
    bufferSize *= subresourceRange.layerCount;

    IBuffer* uploadBuffer = nullptr;
    Offset uploadBufferOffset = 0;
    m_transientHeap->allocateStagingBuffer(bufferSize, uploadBuffer, uploadBufferOffset, MemoryType::Upload);

    // Copy into upload buffer
    {
        int subresourceCounter = 0;

        uint8_t* dstData;
        uploadBuffer->map(nullptr, (void**)&dstData);
        dstData += uploadBufferOffset;
        uint8_t* dstDataStart;
        dstDataStart = dstData;

        Offset dstSubresourceOffset = 0;
        for (GfxIndex i = 0; i < subresourceRange.layerCount; ++i)
        {
            for (GfxIndex j = 0; j < (GfxCount)mipSizes.size(); ++j)
            {
                const auto& mipSize = mipSizes[j];

                int subresourceIndex = subresourceCounter++;
                auto initSubresource = subresourceData[subresourceIndex];

                const ptrdiff_t srcRowStride = (ptrdiff_t)initSubresource.strideY;
                const ptrdiff_t srcLayerStride = (ptrdiff_t)initSubresource.strideZ;

                auto dstRowSizeInBytes = calcRowSize(desc.format, mipSize.width);
                auto numRows = calcNumRows(desc.format, mipSize.height);
                auto dstLayerSizeInBytes = dstRowSizeInBytes * numRows;

                const uint8_t* srcLayer = (const uint8_t*)initSubresource.data;
                uint8_t* dstLayer = dstData + dstSubresourceOffset;

                for (int k = 0; k < mipSize.depth; k++)
                {
                    const uint8_t* srcRow = srcLayer;
                    uint8_t* dstRow = dstLayer;

                    for (GfxCount l = 0; l < numRows; l++)
                    {
                        ::memcpy(dstRow, srcRow, dstRowSizeInBytes);

                        dstRow += dstRowSizeInBytes;
                        srcRow += srcRowStride;
                    }

                    dstLayer += dstLayerSizeInBytes;
                    srcLayer += srcLayerStride;
                }

                dstSubresourceOffset += dstLayerSizeInBytes * mipSize.depth;
            }
        }
        uploadBuffer->unmap(nullptr);
    }
    {
        Offset srcOffset = uploadBufferOffset;
        for (GfxIndex i = 0; i < subresourceRange.layerCount; ++i)
        {
            for (GfxIndex j = 0; j < (GfxCount)mipSizes.size(); ++j)
            {
                const auto& mipSize = mipSizes[j];

                auto rowSizeInBytes = calcRowSize(desc.format, mipSize.width);
                auto numRows = calcNumRows(desc.format, mipSize.height);

                // https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkBufferImageCopy.html
                // bufferRowLength and bufferImageHeight specify the data in buffer
                // memory as a subregion of a larger two- or three-dimensional image,
                // and control the addressing calculations of data in buffer memory. If
                // either of these values is zero, that aspect of the buffer memory is
                // considered to be tightly packed according to the imageExtent.

                VkBufferImageCopy region = {};

                region.bufferOffset = srcOffset;
                region.bufferRowLength = 0; // rowSizeInBytes;
                region.bufferImageHeight = 0;

                region.imageSubresource.aspectMask = getAspectMaskFromFormat(dstTexture->m_vkformat);
                region.imageSubresource.mipLevel = subresourceRange.mipLevel + uint32_t(j);
                region.imageSubresource.baseArrayLayer = subresourceRange.baseArrayLayer + i;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = {0, 0, 0};
                region.imageExtent = {uint32_t(mipSize.width), uint32_t(mipSize.height), uint32_t(mipSize.depth)};

                // Do the copy (do all depths in a single go)
                api.vkCmdCopyBufferToImage(
                    m_cmdBuffer,
                    checked_cast<BufferImpl*>(uploadBuffer)->m_buffer.m_buffer,
                    dstTexture->m_image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &region
                );

                // Next
                srcOffset += rowSizeInBytes * numRows * mipSize.depth;
            }
        }
    }
}
#endif
