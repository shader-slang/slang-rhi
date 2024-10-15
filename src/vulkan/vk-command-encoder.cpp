#include "vk-command-encoder.h"
#include "vk-buffer.h"
#include "vk-command-buffer.h"
#include "vk-helper-functions.h"
#include "vk-query.h"
#include "vk-shader-object.h"
#include "vk-shader-program.h"
#include "vk-shader-table.h"
#include "vk-texture.h"
#include "vk-texture-view.h"
#include "vk-transient-heap.h"
#include "vk-device.h"
#include "vk-api.h"
#include "vk-acceleration-structure.h"
#include "vk-query.h"

#include "core/short_vector.h"

#include <vector>

namespace rhi::vk {

struct BindingContextImpl : public RootBindingContext
{
    CommandEncoderImpl* encoder;
    VkPipelineLayout pipelineLayout;

    void writeBuffer(BufferImpl* buffer, size_t offset, size_t size, void const* data) override
    {
        IBuffer* stagingBuffer = nullptr;
        Offset stagingBufferOffset = 0;
        encoder->m_transientHeap->allocateStagingBuffer(size, stagingBuffer, stagingBufferOffset, MemoryType::Upload);

        BufferImpl* stagingBufferImpl = checked_cast<BufferImpl*>(stagingBuffer);

        auto& api = encoder->m_device->m_api;

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
        api.vkCmdCopyBuffer(
            encoder->m_cmdBuffer,
            stagingBufferImpl->m_buffer.m_buffer,
            buffer->m_buffer.m_buffer,
            1,
            &copyInfo
        );
    }

    void writePushConstants(VkPushConstantRange range, const void* data) override
    {
        auto& api = encoder->m_device->m_api;
        api.vkCmdPushConstants(encoder->m_cmdBuffer, pipelineLayout, range.stageFlags, range.offset, range.size, data);
    }
};

Result CommandEncoderImpl::init(DeviceImpl* device, CommandQueueImpl* queue)
{
    m_device = device;
    m_transientHeap = new TransientResourceHeapImpl();
    SLANG_RETURN_ON_FAIL(m_transientHeap->init({}, m_device));

    SLANG_RETURN_ON_FAIL(m_transientHeap->allocateCommandBuffer(m_commandBuffer.writeRef()));

    m_cmdBuffer = m_commandBuffer->m_commandBuffer;

    auto& api = m_device->m_api;
    VkCommandBufferBeginInfo beginInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    api.vkBeginCommandBuffer(m_cmdBuffer, &beginInfo);

    return SLANG_OK;
}

Result CommandEncoderImpl::createRootShaderObject(ShaderProgram* program, ShaderObjectBase** outObject)
{
    RefPtr<RootShaderObjectImpl> object = new RootShaderObjectImpl();
    SLANG_RETURN_ON_FAIL(object->init(m_device, checked_cast<ShaderProgramImpl*>(program)->m_rootObjectLayout));
    returnRefPtr(outObject, object);
    return SLANG_OK;
}

void CommandEncoderImpl::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    auto& api = m_device->m_api;

    BufferImpl* dstBuffer = checked_cast<BufferImpl*>(dst);
    BufferImpl* srcBuffer = checked_cast<BufferImpl*>(src);

    requireBufferState(dstBuffer, ResourceState::CopyDestination);
    requireBufferState(srcBuffer, ResourceState::CopySource);
    commitBarriers();

    VkBufferCopy copyRegion;
    copyRegion.dstOffset = dstOffset;
    copyRegion.srcOffset = srcOffset;
    copyRegion.size = size;

    // Note: Vulkan puts the source buffer first in the copy
    // command, going against the dominant tradition for copy
    // operations in C/C++.
    //
    api.vkCmdCopyBuffer(
        m_cmdBuffer,
        srcBuffer->m_buffer.m_buffer,
        dstBuffer->m_buffer.m_buffer,
        /* regionCount: */ 1,
        &copyRegion
    );
}

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

    const TextureDesc& srcDesc = srcTexture->m_desc;
    VkImageLayout srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const TextureDesc& dstDesc = dstTexture->m_desc;
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
    region.srcSubresource.aspectMask = VulkanUtil::getAspectMask(TextureAspect::All, srcTexture->m_vkformat);
    region.srcSubresource.baseArrayLayer = srcSubresource.baseArrayLayer;
    region.srcSubresource.mipLevel = srcSubresource.mipLevel;
    region.srcSubresource.layerCount = srcSubresource.layerCount;
    region.srcOffset = {(int32_t)srcOffset.x, (int32_t)srcOffset.y, (int32_t)srcOffset.z};
    region.dstSubresource.aspectMask = VulkanUtil::getAspectMask(TextureAspect::All, dstTexture->m_vkformat);
    region.dstSubresource.baseArrayLayer = dstSubresource.baseArrayLayer;
    region.dstSubresource.mipLevel = dstSubresource.mipLevel;
    region.dstSubresource.layerCount = dstSubresource.layerCount;
    region.dstOffset = {(int32_t)dstOffset.x, (int32_t)dstOffset.y, (int32_t)dstOffset.z};
    region.extent = {(uint32_t)extent.width, (uint32_t)extent.height, (uint32_t)extent.depth};

    auto& api = m_device->m_api;
    api.vkCmdCopyImage(
        m_cmdBuffer,
        srcTexture->m_image,
        srcImageLayout,
        dstTexture->m_image,
        dstImageLayout,
        1,
        &region
    );
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

void CommandEncoderImpl::clearBuffer(IBuffer* buffer, const BufferRange* range)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);

    requireBufferState(bufferImpl, ResourceState::CopyDestination);
    commitBarriers();

    auto& api = m_device->m_api;
    uint64_t offset = range ? range->offset : 0;
    uint64_t size = range ? range->size : bufferImpl->m_desc.size;
    api.vkCmdFillBuffer(m_cmdBuffer, bufferImpl->m_buffer.m_buffer, offset, size, 0);
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
    QueryPoolImpl* poolImpl = checked_cast<QueryPoolImpl*>(queryPool);
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);

    requireBufferState(bufferImpl, ResourceState::CopyDestination);
    commitBarriers();

    auto& api = m_device->m_api;
    api.vkCmdCopyQueryPoolResults(
        m_cmdBuffer,
        poolImpl->m_pool,
        index,
        count,
        bufferImpl->m_buffer.m_buffer,
        offset,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
    );
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

    VkBufferImageCopy region = {};
    region.bufferOffset = dstOffset;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VulkanUtil::getAspectMask(TextureAspect::All, srcTexture->m_vkformat);
    region.imageSubresource.mipLevel = srcSubresource.mipLevel;
    region.imageSubresource.baseArrayLayer = srcSubresource.baseArrayLayer;
    region.imageSubresource.layerCount = srcSubresource.layerCount;
    region.imageOffset = {(int32_t)srcOffset.x, (int32_t)srcOffset.y, (int32_t)srcOffset.z};
    region.imageExtent = {uint32_t(extent.width), uint32_t(extent.height), uint32_t(extent.depth)};

    auto& api = m_device->m_api;
    api.vkCmdCopyImageToBuffer(
        m_cmdBuffer,
        srcTexture->m_image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dstBuffer->m_buffer.m_buffer,
        1,
        &region
    );
}

void CommandEncoderImpl::beginRenderPass(const RenderPassDesc& desc)
{
    auto& api = m_device->m_api;

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
        api.m_deviceProperties.limits.maxFramebufferWidth,
        api.m_deviceProperties.limits.maxFramebufferHeight
    };
    uint32_t layerCount = 1;

    for (GfxIndex i = 0; i < desc.colorAttachmentCount; ++i)
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
            const auto& attachment = *desc.depthStencilAttachment;
            depthAttachmentInfo.imageView = checked_cast<TextureViewImpl*>(attachment.view)->getView().imageView;
            depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachmentInfo.loadOp = translateLoadOp(attachment.depthLoadOp);
            depthAttachmentInfo.storeOp = translateStoreOp(attachment.depthStoreOp);
            depthAttachmentInfo.clearValue.depthStencil.depth = attachment.depthClearValue;
        }
        if (VulkanUtil::isStencilFormat(view->m_texture->m_vkformat))
        {
            hasStencilAttachment = true;
            const auto& attachment = *desc.depthStencilAttachment;
            stencilAttachmentInfo.imageView = checked_cast<TextureViewImpl*>(attachment.view)->getView().imageView;
            stencilAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            stencilAttachmentInfo.loadOp = translateLoadOp(attachment.stencilLoadOp);
            stencilAttachmentInfo.storeOp = translateStoreOp(attachment.stencilStoreOp);
            stencilAttachmentInfo.clearValue.depthStencil.stencil = attachment.stencilClearValue;
        }
    }

    VkRenderingInfoKHR renderingInfo = {VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    renderingInfo.renderArea = renderArea;
    renderingInfo.layerCount = layerCount;
    renderingInfo.colorAttachmentCount = colorAttachmentInfos.size();
    renderingInfo.pColorAttachments = colorAttachmentInfos.data();
    renderingInfo.pDepthAttachment = hasDepthAttachment ? &depthAttachmentInfo : nullptr;
    renderingInfo.pStencilAttachment = hasStencilAttachment ? &stencilAttachmentInfo : nullptr;

    api.vkCmdBeginRenderingKHR(m_cmdBuffer, &renderingInfo);

    m_renderPassActive = true;
}

void CommandEncoderImpl::endRenderPass()
{
    auto& api = m_device->m_api;
    api.vkCmdEndRenderingKHR(m_cmdBuffer);

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

    auto& api = m_device->m_api;

    if (updatePipeline)
    {
        m_renderPipeline = checked_cast<RenderPipelineImpl*>(state.pipeline);
        api.vkCmdBindPipeline(m_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_renderPipeline->m_pipeline);
    }

    if (updateRootObject)
    {
        m_rootObject = checked_cast<RootShaderObjectImpl*>(state.rootObject);
        if (bindRootObject(m_rootObject, m_renderPipeline->m_rootObjectLayout, VK_PIPELINE_BIND_POINT_GRAPHICS) !=
            SLANG_OK)
        {
            // TODO issue a message?
            return;
        }
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
        for (Index i = 0; i < state.vertexBufferCount; ++i)
        {
            BufferImpl* buffer = checked_cast<BufferImpl*>(state.vertexBuffers[i].buffer);
            requireBufferState(buffer, ResourceState::VertexBuffer);

            vertexBuffers[i] = buffer->m_buffer.m_buffer;
            offsets[i] = state.vertexBuffers[i].offset;
        }
        api.vkCmdBindVertexBuffers(m_cmdBuffer, (uint32_t)0, (uint32_t)state.vertexBufferCount, vertexBuffers, offsets);
    }

    if (updateIndexBuffer)
    {
        if (state.indexBuffer.buffer)
        {
            BufferImpl* buffer = checked_cast<BufferImpl*>(state.indexBuffer.buffer);
            Offset offset = state.indexBuffer.offset;
            requireBufferState(buffer, ResourceState::IndexBuffer);

            VkIndexType indexType =
                state.indexFormat == IndexFormat::UInt32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;

            api.vkCmdBindIndexBuffer(m_cmdBuffer, buffer->m_buffer.m_buffer, (VkDeviceSize)offset, indexType);
        }
        else
        {
            api.vkCmdBindIndexBuffer(m_cmdBuffer, VK_NULL_HANDLE, 0, VK_INDEX_TYPE_UINT32);
        }
    }

    if (updateViewports)
    {
        VkViewport viewports[SLANG_COUNT_OF(state.viewports)];
        for (GfxIndex i = 0; i < state.viewportCount; ++i)
        {
            const Viewport& src = state.viewports[i];
            VkViewport& dst = viewports[i];
            dst.x = src.originX;
            dst.y = src.originY;
            dst.width = src.extentX;
            dst.height = -src.extentY;
            dst.minDepth = src.minZ;
            dst.maxDepth = src.maxZ;
        }
        api.vkCmdSetViewport(m_cmdBuffer, 0, uint32_t(state.viewportCount), viewports);
    }

    if (updateScissorRects)
    {
        VkRect2D scissorRects[SLANG_COUNT_OF(state.scissorRects)];
        for (GfxIndex i = 0; i < state.scissorRectCount; ++i)
        {
            const ScissorRect& src = state.scissorRects[i];
            VkRect2D& dst = scissorRects[i];
            dst.offset.x = int32_t(src.minX);
            dst.offset.y = int32_t(src.minY);
            dst.extent.width = uint32_t(src.maxX - src.minX);
            dst.extent.height = uint32_t(src.maxY - src.minY);
        }
        api.vkCmdSetScissor(m_cmdBuffer, 0, uint32_t(state.scissorRectCount), scissorRects);
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

    auto& api = m_device->m_api;
    api.vkCmdDraw(
        m_cmdBuffer,
        args.vertexCount,
        args.instanceCount,
        args.startVertexLocation,
        args.startInstanceLocation
    );
}

void CommandEncoderImpl::drawIndexed(const DrawArguments& args)
{
    if (!m_renderStateValid)
        return;

    auto& api = m_device->m_api;
    api.vkCmdDrawIndexed(
        m_cmdBuffer,
        args.vertexCount,
        args.instanceCount,
        args.startIndexLocation,
        args.startVertexLocation,
        args.startInstanceLocation
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

    // Vulkan does not support sourcing the count from a buffer.
    if (countBuffer)
        return;

    auto argBufferImpl = checked_cast<BufferImpl*>(argBuffer);
    requireBufferState(argBufferImpl, ResourceState::IndirectArgument);
    commitBarriers();

    auto& api = m_device->m_api;
    api.vkCmdDrawIndirect(
        m_cmdBuffer,
        argBufferImpl->m_buffer.m_buffer,
        argOffset,
        maxDrawCount,
        sizeof(VkDrawIndirectCommand)
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

    // Vulkan does not support sourcing the count from a buffer.
    if (countBuffer)
        return;

    auto argBufferImpl = checked_cast<BufferImpl*>(argBuffer);
    requireBufferState(argBufferImpl, ResourceState::IndirectArgument);
    commitBarriers();

    auto& api = m_device->m_api;
    api.vkCmdDrawIndexedIndirect(
        m_cmdBuffer,
        argBufferImpl->m_buffer.m_buffer,
        argOffset,
        maxDrawCount,
        sizeof(VkDrawIndexedIndirectCommand)
    );
}

void CommandEncoderImpl::drawMeshTasks(int x, int y, int z)
{
    auto& api = m_device->m_api;
    api.vkCmdDrawMeshTasksEXT(m_cmdBuffer, x, y, z);
}

void CommandEncoderImpl::setComputeState(const ComputeState& state)
{
    if (m_renderPassActive)
        return;

    bool updatePipeline = !m_computeStateValid || state.pipeline != m_computeState.pipeline;
    bool updateRootObject = updatePipeline || state.rootObject != m_computeState.rootObject;

    auto& api = m_device->m_api;

    if (updatePipeline)
    {
        m_computePipeline = checked_cast<ComputePipelineImpl*>(state.pipeline);
        api.vkCmdBindPipeline(m_cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline->m_pipeline);
    }

    if (updateRootObject)
    {
        m_rootObject = checked_cast<RootShaderObjectImpl*>(state.rootObject);
        if (bindRootObject(m_rootObject, m_computePipeline->m_rootObjectLayout, VK_PIPELINE_BIND_POINT_COMPUTE) !=
            SLANG_OK)
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

    auto& api = m_device->m_api;
    api.vkCmdDispatch(m_cmdBuffer, x, y, z);
}

void CommandEncoderImpl::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    if (!m_computeStateValid)
        return;

    BufferImpl* argBufferImpl = checked_cast<BufferImpl*>(argBuffer);
    requireBufferState(argBufferImpl, ResourceState::IndirectArgument);
    commitBarriers();

    auto& api = m_device->m_api;
    api.vkCmdDispatchIndirect(m_cmdBuffer, argBufferImpl->m_buffer.m_buffer, offset);
}

void CommandEncoderImpl::setRayTracingState(const RayTracingState& state)
{
    if (m_renderPassActive)
        return;

    bool updatePipeline = !m_rayTracingStateValid || state.pipeline != m_rayTracingState.pipeline;
    bool updateRootObject = updatePipeline || state.rootObject != m_rayTracingState.rootObject;
    bool updateShaderTable = !m_rayTracingStateValid || state.shaderTable != m_rayTracingState.shaderTable;

    auto& api = m_device->m_api;

    if (updatePipeline)
    {
        m_rayTracingPipeline = checked_cast<RayTracingPipelineImpl*>(state.pipeline);
        api.vkCmdBindPipeline(m_cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rayTracingPipeline->m_pipeline);
    }

    if (updateRootObject)
    {
        m_rootObject = checked_cast<RootShaderObjectImpl*>(state.rootObject);
        if (bindRootObject(
                m_rootObject,
                m_rayTracingPipeline->m_rootObjectLayout,
                VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR
            ) != SLANG_OK)
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

        auto rtProps = api.m_rtProperties;
        auto alignedHandleSize =
            VulkanUtil::calcAligned(rtProps.shaderGroupHandleSize, rtProps.shaderGroupHandleAlignment);

        // Raygen index is set at dispatch time.
        m_rayGenTableAddr = shaderTableAddr;
        m_raygenSBT.stride = VulkanUtil::calcAligned(alignedHandleSize, rtProps.shaderGroupBaseAlignment);
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
    m_rayTracingState = state;
}

void CommandEncoderImpl::dispatchRays(GfxIndex raygenShaderIndex, GfxCount width, GfxCount height, GfxCount depth)
{
    if (!m_rayTracingStateValid)
        return;

    auto& api = m_device->m_api;

    m_raygenSBT.deviceAddress = m_rayGenTableAddr + raygenShaderIndex * m_raygenSBT.stride;

    api.vkCmdTraceRaysKHR(
        m_cmdBuffer,
        &m_raygenSBT,
        &m_missSBT,
        &m_hitSBT,
        &m_callableSBT,
        (uint32_t)width,
        (uint32_t)height,
        (uint32_t)depth
    );
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
    AccelerationStructureBuildGeometryInfoBuilder geomInfoBuilder;
    if (geomInfoBuilder.build(desc, m_device->m_debugCallback) != SLANG_OK)
        return;

    geomInfoBuilder.buildInfo.dstAccelerationStructure = checked_cast<AccelerationStructureImpl*>(dst)->m_vkHandle;
    if (src)
    {
        geomInfoBuilder.buildInfo.srcAccelerationStructure = checked_cast<AccelerationStructureImpl*>(src)->m_vkHandle;
    }
    geomInfoBuilder.buildInfo.scratchData.deviceAddress = scratchBuffer.getDeviceAddress();

    std::vector<VkAccelerationStructureBuildRangeInfoKHR> rangeInfos;
    rangeInfos.resize(geomInfoBuilder.primitiveCounts.size());
    for (Index i = 0; i < geomInfoBuilder.primitiveCounts.size(); i++)
    {
        auto& rangeInfo = rangeInfos[i];
        rangeInfo.primitiveCount = geomInfoBuilder.primitiveCounts[i];
        rangeInfo.firstVertex = 0;
        rangeInfo.primitiveOffset = 0;
        rangeInfo.transformOffset = 0;
    }

    auto rangeInfoPtr = rangeInfos.data();
    m_device->m_api.vkCmdBuildAccelerationStructuresKHR(
        m_commandBuffer->m_commandBuffer,
        1,
        &geomInfoBuilder.buildInfo,
        &rangeInfoPtr
    );

    if (propertyQueryCount)
    {
        _memoryBarrier(1, &dst, AccessFlag::Write, AccessFlag::Read);
        _queryAccelerationStructureProperties(1, &dst, propertyQueryCount, queryDescs);
    }
}

void CommandEncoderImpl::copyAccelerationStructure(
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    AccelerationStructureCopyMode mode
)
{
    VkCopyAccelerationStructureInfoKHR copyInfo = {VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
    copyInfo.src = checked_cast<AccelerationStructureImpl*>(src)->m_vkHandle;
    copyInfo.dst = checked_cast<AccelerationStructureImpl*>(dst)->m_vkHandle;
    switch (mode)
    {
    case AccelerationStructureCopyMode::Clone:
        copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR;
        break;
    case AccelerationStructureCopyMode::Compact:
        copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
        break;
    default:
        m_device->handleMessage(
            DebugMessageType::Error,
            DebugMessageSource::Layer,
            "Unsupported AccelerationStructureCopyMode."
        );
        return;
    }
    m_device->m_api.vkCmdCopyAccelerationStructureKHR(m_cmdBuffer, &copyInfo);
}

void CommandEncoderImpl::queryAccelerationStructureProperties(
    GfxCount accelerationStructureCount,
    IAccelerationStructure* const* accelerationStructures,
    GfxCount queryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    _queryAccelerationStructureProperties(accelerationStructureCount, accelerationStructures, queryCount, queryDescs);
}

void CommandEncoderImpl::serializeAccelerationStructure(BufferWithOffset dst, IAccelerationStructure* src)
{
    VkCopyAccelerationStructureToMemoryInfoKHR copyInfo = {
        VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_TO_MEMORY_INFO_KHR
    };
    copyInfo.src = checked_cast<AccelerationStructureImpl*>(src)->m_vkHandle;
    copyInfo.dst.deviceAddress = dst.getDeviceAddress();
    copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_SERIALIZE_KHR;
    m_device->m_api.vkCmdCopyAccelerationStructureToMemoryKHR(m_cmdBuffer, &copyInfo);
}

void CommandEncoderImpl::deserializeAccelerationStructure(IAccelerationStructure* dst, BufferWithOffset src)
{
    VkCopyMemoryToAccelerationStructureInfoKHR copyInfo = {
        VK_STRUCTURE_TYPE_COPY_MEMORY_TO_ACCELERATION_STRUCTURE_INFO_KHR
    };
    copyInfo.src.deviceAddress = src.getDeviceAddress();
    copyInfo.dst = checked_cast<AccelerationStructureImpl*>(dst)->m_vkHandle;
    copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_DESERIALIZE_KHR;
    m_device->m_api.vkCmdCopyMemoryToAccelerationStructureKHR(m_cmdBuffer, &copyInfo);
}

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
    auto& api = m_device->m_api;
    if (api.vkCmdBeginDebugUtilsLabelEXT)
    {
        VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = name;
        label.color[0] = rgbColor[0];
        label.color[1] = rgbColor[1];
        label.color[2] = rgbColor[2];
        label.color[3] = 1.0f;
        api.vkCmdBeginDebugUtilsLabelEXT(m_cmdBuffer, &label);
    }
}

void CommandEncoderImpl::endDebugEvent()
{
    auto& api = m_device->m_api;
    if (api.vkCmdEndDebugUtilsLabelEXT)
    {
        api.vkCmdEndDebugUtilsLabelEXT(m_cmdBuffer);
    }
}

void CommandEncoderImpl::writeTimestamp(IQueryPool* pool, GfxIndex index)
{
    _writeTimestamp(m_device->m_api, m_cmdBuffer, pool, index);
}

Result CommandEncoderImpl::finish(ICommandBuffer** outCommandBuffer)
{
    if (!m_commandBuffer)
    {
        return SLANG_FAIL;
    }
    endPassEncoder();

    // Transition all resources back to their default states.
    m_stateTracking.requireDefaultStates();
    commitBarriers();
    m_stateTracking.clear();

    auto& api = m_device->m_api;
    // TODO hande command buffer for uploads
#if 0
    if (!m_isPreCommandBufferEmpty)
    {
        // `preCmdBuffer` contains buffer transfer commands for shader object
        // uniform buffers, and we need a memory barrier here to ensure the
        // transfers are visible to shaders.
        VkMemoryBarrier memBarrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        memBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        vkAPI.vkCmdPipelineBarrier(
            m_preCommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            0,
            1,
            &memBarrier,
            0,
            nullptr,
            0,
            nullptr
        );
        vkAPI.vkEndCommandBuffer(m_preCommandBuffer);
    }
#endif
    api.vkEndCommandBuffer(m_cmdBuffer);
    m_commandBuffer = nullptr;
    m_cmdBuffer = VK_NULL_HANDLE;
    return SLANG_OK;
}

Result CommandEncoderImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkCommandBuffer;
    outHandle->value = (uint64_t)m_cmdBuffer;
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
    auto& api = m_device->m_api;

    short_vector<VkBufferMemoryBarrier, 16> bufferBarriers;
    short_vector<VkImageMemoryBarrier, 16> imageBarriers;

    VkPipelineStageFlags activeBeforeStageFlags = VkPipelineStageFlags(0);
    VkPipelineStageFlags activeAfterStageFlags = VkPipelineStageFlags(0);

    auto submitBufferBarriers = [&]()
    {
        api.vkCmdPipelineBarrier(
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
        api.vkCmdPipelineBarrier(
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


void CommandEncoderImpl::_memoryBarrier(
    int count,
    IAccelerationStructure* const* structures,
    AccessFlag srcAccess,
    AccessFlag destAccess
)
{
    short_vector<VkBufferMemoryBarrier> memBarriers;
    memBarriers.resize(count);
    for (int i = 0; i < count; i++)
    {
        memBarriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        memBarriers[i].pNext = nullptr;
        memBarriers[i].dstAccessMask = translateAccelerationStructureAccessFlag(destAccess);
        memBarriers[i].srcAccessMask = translateAccelerationStructureAccessFlag(srcAccess);
        memBarriers[i].srcQueueFamilyIndex = m_device->m_queueFamilyIndex;
        memBarriers[i].dstQueueFamilyIndex = m_device->m_queueFamilyIndex;

        auto asImpl = checked_cast<AccelerationStructureImpl*>(structures[i]);
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

void CommandEncoderImpl::_queryAccelerationStructureProperties(
    GfxCount accelerationStructureCount,
    IAccelerationStructure* const* accelerationStructures,
    GfxCount queryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    short_vector<VkAccelerationStructureKHR> vkHandles;
    vkHandles.resize(accelerationStructureCount);
    for (GfxIndex i = 0; i < accelerationStructureCount; i++)
    {
        vkHandles[i] = checked_cast<AccelerationStructureImpl*>(accelerationStructures[i])->m_vkHandle;
    }
    for (GfxIndex i = 0; i < queryCount; i++)
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

Result CommandEncoderImpl::bindRootObject(
    RootShaderObjectImpl* rootObject,
    RootShaderObjectLayout* rootObjectLayout,
    VkPipelineBindPoint bindPoint
)
{
    // We will set up the context required when binding shader objects
    // to the pipeline. Note that this is mostly just being packaged
    // together to minimize the number of parameters that have to
    // be dealt with in the complex recursive call chains.
    //
    BindingContextImpl context;
    context.pipelineLayout = rootObjectLayout->m_pipelineLayout;
    context.device = m_device;
    context.descriptorSetAllocator = &m_transientHeap->m_descSetAllocator;
    context.pushConstantRanges = span(rootObjectLayout->getAllPushConstantRanges());

    // The context includes storage for the descriptor sets we will bind,
    // and the number of sets we need to make space for is determined
    // by the specialized program layout.
    //
    std::vector<VkDescriptorSet> descriptorSetsStorage;

    context.descriptorSets = &descriptorSetsStorage;

    rootObject->setResourceStates(m_stateTracking);
    commitBarriers();

    // We kick off recursive binding of shader objects to the pipeline (plus
    // the state in `context`).
    //
    // Note: this logic will directly write any push-constant ranges needed,
    // and will also fill in any descriptor sets. Currently it does not
    // *bind* the descriptor sets it fills in.
    //
    // TODO: It could probably bind the descriptor sets as well.
    //
    SLANG_RETURN_ON_FAIL(rootObject->bindAsRoot(context, rootObjectLayout));

    // Once we've filled in all the descriptor sets, we bind them
    // to the pipeline at once.
    //
    if (descriptorSetsStorage.size() > 0)
    {
        m_device->m_api.vkCmdBindDescriptorSets(
            m_cmdBuffer,
            bindPoint,
            rootObjectLayout->m_pipelineLayout,
            0,
            (uint32_t)descriptorSetsStorage.size(),
            descriptorSetsStorage.data(),
            0,
            nullptr
        );
    }

    return SLANG_OK;
}

#if 0

void PassEncoderImpl::_uploadBufferData(
    VulkanApi* api,
    VkCommandBuffer commandBuffer,
    TransientResourceHeapImpl* transientHeap,
    BufferImpl* buffer,
    Offset offset,
    Size size,
    void* data
)
{
    IBuffer* stagingBuffer = nullptr;
    Offset stagingBufferOffset = 0;
    transientHeap->allocateStagingBuffer(size, stagingBuffer, stagingBufferOffset, MemoryType::Upload);

    BufferImpl* stagingBufferImpl = checked_cast<BufferImpl*>(stagingBuffer);

    void* mappedData = nullptr;
    SLANG_VK_CHECK(api->vkMapMemory(
        api->m_device,
        stagingBufferImpl->m_buffer.m_memory,
        0,
        stagingBufferOffset + size,
        0,
        &mappedData
    ));
    memcpy((char*)mappedData + stagingBufferOffset, data, size);
    api->vkUnmapMemory(api->m_device, stagingBufferImpl->m_buffer.m_memory);

    // Copy from staging buffer to real buffer
    VkBufferCopy copyInfo = {};
    copyInfo.size = size;
    copyInfo.dstOffset = offset;
    copyInfo.srcOffset = stagingBufferOffset;
    api->vkCmdCopyBuffer(commandBuffer, stagingBufferImpl->m_buffer.m_buffer, buffer->m_buffer.m_buffer, 1, &copyInfo);
}

void PassEncoderImpl::uploadBufferDataImpl(IBuffer* buffer, Offset offset, Size size, void* data)
{
    m_vkPreCommandBuffer = m_commandBuffer->getPreCommandBuffer();
    _uploadBufferData(
        m_api,
        m_vkPreCommandBuffer,
        m_commandBuffer->m_transientHeap.get(),
        checked_cast<BufferImpl*>(buffer),
        offset,
        size,
        data
    );
}

#if 0
void ResourcePassEncoderImpl::_clearColorImage(TextureViewImpl* viewImpl, ClearValue* clearValue)
{
    auto& api = m_commandBuffer->m_device->m_api;
    auto layout = viewImpl->m_layout;
    if (layout != VK_IMAGE_LAYOUT_GENERAL && layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        m_commandBuffer->m_device->_transitionImageLayout(
            m_commandBuffer->m_commandBuffer,
            viewImpl->m_texture->m_image,
            viewImpl->m_texture->m_vkformat,
            *viewImpl->m_texture->getDesc(),
            viewImpl->m_layout,
            layout
        );
    }

    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseArrayLayer = viewImpl->m_desc.subresourceRange.baseArrayLayer;
    subresourceRange.baseMipLevel = viewImpl->m_desc.subresourceRange.mipLevel;
    subresourceRange.layerCount = viewImpl->m_desc.subresourceRange.layerCount;
    subresourceRange.levelCount = 1;

    VkClearColorValue vkClearColor = {};
    memcpy(vkClearColor.float32, clearValue->color.floatValues, sizeof(float) * 4);

    api.vkCmdClearColorImage(
        m_commandBuffer->m_commandBuffer,
        viewImpl->m_texture->m_image,
        layout,
        &vkClearColor,
        1,
        &subresourceRange
    );

    if (layout != viewImpl->m_layout)
    {
        m_commandBuffer->m_device->_transitionImageLayout(
            m_commandBuffer->m_commandBuffer,
            viewImpl->m_texture->m_image,
            viewImpl->m_texture->m_vkformat,
            *viewImpl->m_texture->getDesc(),
            layout,
            viewImpl->m_layout
        );
    }
}

void ResourcePassEncoderImpl::_clearDepthImage(
    TextureViewImpl* viewImpl,
    ClearValue* clearValue,
    ClearResourceViewFlags::Enum flags
)
{
    auto& api = m_commandBuffer->m_device->m_api;
    auto layout = viewImpl->m_layout;
    if (layout != VK_IMAGE_LAYOUT_GENERAL && layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        m_commandBuffer->m_device->_transitionImageLayout(
            m_commandBuffer->m_commandBuffer,
            viewImpl->m_texture->m_image,
            viewImpl->m_texture->m_vkformat,
            *viewImpl->m_texture->getDesc(),
            viewImpl->m_layout,
            layout
        );
    }

    VkImageSubresourceRange subresourceRange = {};
    if (flags & ClearResourceViewFlags::ClearDepth)
    {
        if (VulkanUtil::isDepthFormat(viewImpl->m_texture->m_vkformat))
        {
            subresourceRange.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
        }
    }
    if (flags & ClearResourceViewFlags::ClearStencil)
    {
        if (VulkanUtil::isStencilFormat(viewImpl->m_texture->m_vkformat))
        {
            subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }
    subresourceRange.baseArrayLayer = viewImpl->m_desc.subresourceRange.baseArrayLayer;
    subresourceRange.baseMipLevel = viewImpl->m_desc.subresourceRange.mipLevel;
    subresourceRange.layerCount = viewImpl->m_desc.subresourceRange.layerCount;
    subresourceRange.levelCount = 1;

    VkClearDepthStencilValue vkClearValue = {};
    vkClearValue.depth = clearValue->depthStencil.depth;
    vkClearValue.stencil = clearValue->depthStencil.stencil;

    api.vkCmdClearDepthStencilImage(
        m_commandBuffer->m_commandBuffer,
        viewImpl->m_texture->m_image,
        layout,
        &vkClearValue,
        1,
        &subresourceRange
    );

    if (layout != viewImpl->m_layout)
    {
        m_commandBuffer->m_device->_transitionImageLayout(
            m_commandBuffer->m_commandBuffer,
            viewImpl->m_texture->m_image,
            viewImpl->m_texture->m_vkformat,
            *viewImpl->m_texture->getDesc(),
            layout,
            viewImpl->m_layout
        );
    }
}

void ResourcePassEncoderImpl::_clearBuffer(
    VkBuffer buffer,
    uint64_t bufferSize,
    const IResourceView::Desc& desc,
    uint32_t clearValue
)
{
    auto& api = m_commandBuffer->m_device->m_api;
    auto clearOffset = desc.bufferRange.offset;
    auto clearSize = desc.bufferRange.size == 0 ? bufferSize - clearOffset : desc.bufferRange.size;
    api.vkCmdFillBuffer(m_commandBuffer->m_commandBuffer, buffer, clearOffset, clearSize, clearValue);
}

void ResourcePassEncoderImpl::clearResourceView(
    IResourceView* view,
    ClearValue* clearValue,
    ClearResourceViewFlags::Enum flags
)
{
    auto& api = m_commandBuffer->m_device->m_api;
    switch (view->getViewDesc()->type)
    {
    case IResourceView::Type::RenderTarget:
    {
        auto viewImpl = checked_cast<TextureViewImpl*>(view);
        _clearColorImage(viewImpl, clearValue);
    }
    break;
    case IResourceView::Type::DepthStencil:
    {
        auto viewImpl = checked_cast<TextureViewImpl*>(view);
        _clearDepthImage(viewImpl, clearValue, flags);
    }
    break;
    case IResourceView::Type::UnorderedAccess:
    {
        auto viewImplBase = checked_cast<ResourceViewImpl*>(view);
        switch (viewImplBase->m_type)
        {
        case ResourceViewImpl::ViewType::Texture:
        {
            auto viewImpl = checked_cast<TextureViewImpl*>(viewImplBase);
            if ((flags & ClearResourceViewFlags::ClearDepth) || (flags & ClearResourceViewFlags::ClearStencil))
            {
                _clearDepthImage(viewImpl, clearValue, flags);
            }
            else
            {
                _clearColorImage(viewImpl, clearValue);
            }
        }
        break;
        case ResourceViewImpl::ViewType::PlainBuffer:
        {
            SLANG_RHI_ASSERT(
                clearValue->color.uintValues[1] == clearValue->color.uintValues[0] &&
                clearValue->color.uintValues[2] == clearValue->color.uintValues[0] &&
                clearValue->color.uintValues[3] == clearValue->color.uintValues[0]
            );
            auto viewImpl = checked_cast<PlainBufferViewImpl*>(viewImplBase);
            uint64_t clearStart = viewImpl->m_desc.bufferRange.offset;
            uint64_t clearSize = viewImpl->m_desc.bufferRange.size;
            if (clearSize == 0)
                clearSize = viewImpl->m_buffer->getDesc()->size - clearStart;
            api.vkCmdFillBuffer(
                m_commandBuffer->m_commandBuffer,
                viewImpl->m_buffer->m_buffer.m_buffer,
                clearStart,
                clearSize,
                clearValue->color.uintValues[0]
            );
        }
        break;
        case ResourceViewImpl::ViewType::TexelBuffer:
        {
            SLANG_RHI_ASSERT(
                clearValue->color.uintValues[1] == clearValue->color.uintValues[0] &&
                clearValue->color.uintValues[2] == clearValue->color.uintValues[0] &&
                clearValue->color.uintValues[3] == clearValue->color.uintValues[0]
            );
            auto viewImpl = checked_cast<TexelBufferViewImpl*>(viewImplBase);
            _clearBuffer(
                viewImpl->m_buffer->m_buffer.m_buffer,
                viewImpl->m_buffer->getDesc()->size,
                viewImpl->m_desc,
                clearValue->color.uintValues[0]
            );
        }
        break;
        }
    }
    break;
    }
}
#endif



// RayTracingPassEncoderImpl


#endif

} // namespace rhi::vk
