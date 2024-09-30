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

#include "core/short_vector.h"

#include <vector>

namespace rhi::vk {

// CommandEncoderImpl

void CommandEncoderImpl::setBufferState(IBuffer* buffer, ResourceState state)
{
    m_commandBuffer->m_stateTracking.setBufferState(static_cast<BufferImpl*>(buffer), state);
}

void CommandEncoderImpl::setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state)
{
    m_commandBuffer->m_stateTracking.setTextureState(static_cast<TextureImpl*>(texture), subresourceRange, state);
}

void CommandEncoderImpl::beginDebugEvent(const char* name, float rgbColor[3])
{
    auto& api = m_commandBuffer->m_device->m_api;
    if (api.vkCmdBeginDebugUtilsLabelEXT)
    {
        VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = name;
        label.color[0] = rgbColor[0];
        label.color[1] = rgbColor[1];
        label.color[2] = rgbColor[2];
        label.color[3] = 1.0f;
        api.vkCmdBeginDebugUtilsLabelEXT(m_commandBuffer->m_commandBuffer, &label);
    }
}

void CommandEncoderImpl::endDebugEvent()
{
    auto& api = m_commandBuffer->m_device->m_api;
    if (api.vkCmdEndDebugUtilsLabelEXT)
    {
        api.vkCmdEndDebugUtilsLabelEXT(m_commandBuffer->m_commandBuffer);
    }
}

void CommandEncoderImpl::writeTimestamp(IQueryPool* queryPool, GfxIndex index)
{
    _writeTimestamp(&m_commandBuffer->m_device->m_api, m_commandBuffer->m_commandBuffer, queryPool, index);
}

int CommandEncoderImpl::getBindPointIndex(VkPipelineBindPoint bindPoint)
{
    switch (bindPoint)
    {
    case VK_PIPELINE_BIND_POINT_GRAPHICS:
        return 0;
    case VK_PIPELINE_BIND_POINT_COMPUTE:
        return 1;
    case VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR:
        return 2;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unknown pipeline type.");
        return -1;
    }
}

void CommandEncoderImpl::init(CommandBufferImpl* commandBuffer)
{
    m_commandBuffer = commandBuffer;
    m_device = commandBuffer->m_device;
    m_vkCommandBuffer = m_commandBuffer->m_commandBuffer;
    m_api = &m_commandBuffer->m_device->m_api;
}

void CommandEncoderImpl::endEncodingImpl()
{
    for (auto& pipeline : m_boundPipelines)
        pipeline = VK_NULL_HANDLE;
}

void CommandEncoderImpl::_uploadBufferData(
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

    BufferImpl* stagingBufferImpl = static_cast<BufferImpl*>(stagingBuffer);

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

void CommandEncoderImpl::uploadBufferDataImpl(IBuffer* buffer, Offset offset, Size size, void* data)
{
    m_vkPreCommandBuffer = m_commandBuffer->getPreCommandBuffer();
    _uploadBufferData(
        m_api,
        m_vkPreCommandBuffer,
        m_commandBuffer->m_transientHeap.get(),
        static_cast<BufferImpl*>(buffer),
        offset,
        size,
        data
    );
}

Result CommandEncoderImpl::bindRootShaderObjectImpl(
    RootShaderObjectImpl* rootShaderObject,
    VkPipelineBindPoint bindPoint
)
{
    // Obtain specialized root layout.
    auto specializedLayout = rootShaderObject->getSpecializedLayout();
    if (!specializedLayout)
        return SLANG_FAIL;

    // We will set up the context required when binding shader objects
    // to the pipeline. Note that this is mostly just being packaged
    // together to minimize the number of parameters that have to
    // be dealt with in the complex recursive call chains.
    //
    RootBindingContext context;
    context.pipelineLayout = specializedLayout->m_pipelineLayout;
    context.device = m_device;
    context.descriptorSetAllocator = &m_commandBuffer->m_transientHeap->m_descSetAllocator;
    context.pushConstantRanges = span(specializedLayout->getAllPushConstantRanges());

    // The context includes storage for the descriptor sets we will bind,
    // and the number of sets we need to make space for is determined
    // by the specialized program layout.
    //
    std::vector<VkDescriptorSet> descriptorSetsStorage;

    context.descriptorSets = &descriptorSetsStorage;

    rootShaderObject->setResourceStates(m_commandBuffer->m_stateTracking);
    m_commandBuffer->commitBarriers();

    // We kick off recursive binding of shader objects to the pipeline (plus
    // the state in `context`).
    //
    // Note: this logic will directly write any push-constant ranges needed,
    // and will also fill in any descriptor sets. Currently it does not
    // *bind* the descriptor sets it fills in.
    //
    // TODO: It could probably bind the descriptor sets as well.
    //
    rootShaderObject->bindAsRoot(this, context, specializedLayout);

    // Once we've filled in all the descriptor sets, we bind them
    // to the pipeline at once.
    //
    if (descriptorSetsStorage.size() > 0)
    {
        m_device->m_api.vkCmdBindDescriptorSets(
            m_commandBuffer->m_commandBuffer,
            bindPoint,
            specializedLayout->m_pipelineLayout,
            0,
            (uint32_t)descriptorSetsStorage.size(),
            descriptorSetsStorage.data(),
            0,
            nullptr
        );
    }

    return SLANG_OK;
}

Result CommandEncoderImpl::setPipelineImpl(IPipeline* state, IShaderObject** outRootObject)
{
    m_currentPipeline = static_cast<PipelineImpl*>(state);
    m_commandBuffer->m_mutableRootShaderObject = nullptr;
    SLANG_RETURN_ON_FAIL(m_commandBuffer->m_rootObject.init(
        m_commandBuffer->m_device,
        m_currentPipeline->getProgram<ShaderProgramImpl>()->m_rootObjectLayout
    ));
    *outRootObject = &m_commandBuffer->m_rootObject;
    return SLANG_OK;
}

Result CommandEncoderImpl::setPipelineWithRootObjectImpl(IPipeline* state, IShaderObject* rootObject)
{
    m_currentPipeline = static_cast<PipelineImpl*>(state);
    m_commandBuffer->m_mutableRootShaderObject = static_cast<MutableRootShaderObjectImpl*>(rootObject);
    return SLANG_OK;
}

Result CommandEncoderImpl::bindRenderState(VkPipelineBindPoint pipelineBindPoint)
{
    auto& api = *m_api;

    // Get specialized pipeline state and bind it.
    //
    RootShaderObjectImpl* rootObjectImpl = m_commandBuffer->m_mutableRootShaderObject
                                               ? m_commandBuffer->m_mutableRootShaderObject.Ptr()
                                               : &m_commandBuffer->m_rootObject;
    RefPtr<Pipeline> newPipeline;
    SLANG_RETURN_ON_FAIL(m_device->maybeSpecializePipeline(m_currentPipeline, rootObjectImpl, newPipeline));
    PipelineImpl* newPipelineImpl = static_cast<PipelineImpl*>(newPipeline.Ptr());

    SLANG_RETURN_ON_FAIL(newPipelineImpl->ensureAPIPipelineCreated());
    m_currentPipeline = newPipelineImpl;

    bindRootShaderObjectImpl(rootObjectImpl, pipelineBindPoint);

    auto pipelineBindPointId = getBindPointIndex(pipelineBindPoint);
    if (m_boundPipelines[pipelineBindPointId] != newPipelineImpl->m_pipeline)
    {
        api.vkCmdBindPipeline(m_vkCommandBuffer, pipelineBindPoint, newPipelineImpl->m_pipeline);
        m_boundPipelines[pipelineBindPointId] = newPipelineImpl->m_pipeline;
    }

    return SLANG_OK;
}

// ResourceCommandEncoderImpl

void ResourceCommandEncoderImpl::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    auto& vkAPI = m_commandBuffer->m_device->m_api;

    BufferImpl* dstBuffer = static_cast<BufferImpl*>(dst);
    BufferImpl* srcBuffer = static_cast<BufferImpl*>(src);

    m_commandBuffer->requireBufferState(dstBuffer, ResourceState::CopyDestination);
    m_commandBuffer->requireBufferState(srcBuffer, ResourceState::CopySource);
    m_commandBuffer->commitBarriers();

    VkBufferCopy copyRegion;
    copyRegion.dstOffset = dstOffset;
    copyRegion.srcOffset = srcOffset;
    copyRegion.size = size;

    // Note: Vulkan puts the source buffer first in the copy
    // command, going against the dominant tradition for copy
    // operations in C/C++.
    //
    vkAPI.vkCmdCopyBuffer(
        m_commandBuffer->m_commandBuffer,
        srcBuffer->m_buffer.m_buffer,
        dstBuffer->m_buffer.m_buffer,
        /* regionCount: */ 1,
        &copyRegion
    );
}

void ResourceCommandEncoderImpl::uploadBufferData(IBuffer* buffer, Offset offset, Size size, void* data)
{
    BufferImpl* bufferImpl = static_cast<BufferImpl*>(buffer);

    m_commandBuffer->requireBufferState(bufferImpl, ResourceState::CopyDestination);
    m_commandBuffer->commitBarriers();

    CommandEncoderImpl::_uploadBufferData(
        m_api,
        m_commandBuffer->m_commandBuffer,
        m_commandBuffer->m_transientHeap.get(),
        bufferImpl,
        offset,
        size,
        data
    );
}


void ResourceCommandEncoderImpl::endEncoding()
{
    // Insert memory barrier to ensure transfers are visible to the GPU.
    auto& vkAPI = m_commandBuffer->m_device->m_api;

    VkMemoryBarrier memBarrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    vkAPI.vkCmdPipelineBarrier(
        m_commandBuffer->m_commandBuffer,
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
}

void ResourceCommandEncoderImpl::copyTexture(
    ITexture* dst,
    SubresourceRange dstSubresource,
    Offset3D dstOffset,
    ITexture* src,
    SubresourceRange srcSubresource,
    Offset3D srcOffset,
    Extents extent
)
{
    TextureImpl* dstTexture = static_cast<TextureImpl*>(dst);
    TextureImpl* srcTexture = static_cast<TextureImpl*>(src);

    m_commandBuffer->requireTextureState(dstTexture, dstSubresource, ResourceState::CopyDestination);
    m_commandBuffer->requireTextureState(srcTexture, srcSubresource, ResourceState::CopySource);
    m_commandBuffer->commitBarriers();

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
    region.srcSubresource.aspectMask = VulkanUtil::getAspectMask(srcSubresource.aspectMask, srcTexture->m_vkformat);
    region.srcSubresource.baseArrayLayer = srcSubresource.baseArrayLayer;
    region.srcSubresource.mipLevel = srcSubresource.mipLevel;
    region.srcSubresource.layerCount = srcSubresource.layerCount;
    region.srcOffset = {(int32_t)srcOffset.x, (int32_t)srcOffset.y, (int32_t)srcOffset.z};
    region.dstSubresource.aspectMask = VulkanUtil::getAspectMask(dstSubresource.aspectMask, dstTexture->m_vkformat);
    region.dstSubresource.baseArrayLayer = dstSubresource.baseArrayLayer;
    region.dstSubresource.mipLevel = dstSubresource.mipLevel;
    region.dstSubresource.layerCount = dstSubresource.layerCount;
    region.dstOffset = {(int32_t)dstOffset.x, (int32_t)dstOffset.y, (int32_t)dstOffset.z};
    region.extent = {(uint32_t)extent.width, (uint32_t)extent.height, (uint32_t)extent.depth};

    auto& api = m_commandBuffer->m_device->m_api;
    api.vkCmdCopyImage(
        m_commandBuffer->m_commandBuffer,
        srcTexture->m_image,
        srcImageLayout,
        dstTexture->m_image,
        dstImageLayout,
        1,
        &region
    );
}

void ResourceCommandEncoderImpl::uploadTextureData(
    ITexture* dst,
    SubresourceRange subresourceRange,
    Offset3D offset,
    Extents extend,
    SubresourceData* subresourceData,
    GfxCount subresourceDataCount
)
{
    TextureImpl* dstTexture = static_cast<TextureImpl*>(dst);

    m_commandBuffer->requireTextureState(dstTexture, subresourceRange, ResourceState::CopyDestination);
    m_commandBuffer->commitBarriers();

    auto& api = m_commandBuffer->m_device->m_api;
    std::vector<Extents> mipSizes;

    VkCommandBuffer commandBuffer = m_commandBuffer->m_commandBuffer;
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
    m_commandBuffer->m_transientHeap
        ->allocateStagingBuffer(bufferSize, uploadBuffer, uploadBufferOffset, MemoryType::Upload);

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
                    commandBuffer,
                    static_cast<BufferImpl*>(uploadBuffer)->m_buffer.m_buffer,
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

#if 0
void ResourceCommandEncoderImpl::_clearColorImage(TextureViewImpl* viewImpl, ClearValue* clearValue)
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

void ResourceCommandEncoderImpl::_clearDepthImage(
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

void ResourceCommandEncoderImpl::_clearBuffer(
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

void ResourceCommandEncoderImpl::clearResourceView(
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
        auto viewImpl = static_cast<TextureViewImpl*>(view);
        _clearColorImage(viewImpl, clearValue);
    }
    break;
    case IResourceView::Type::DepthStencil:
    {
        auto viewImpl = static_cast<TextureViewImpl*>(view);
        _clearDepthImage(viewImpl, clearValue, flags);
    }
    break;
    case IResourceView::Type::UnorderedAccess:
    {
        auto viewImplBase = static_cast<ResourceViewImpl*>(view);
        switch (viewImplBase->m_type)
        {
        case ResourceViewImpl::ViewType::Texture:
        {
            auto viewImpl = static_cast<TextureViewImpl*>(viewImplBase);
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
            auto viewImpl = static_cast<PlainBufferViewImpl*>(viewImplBase);
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
            auto viewImpl = static_cast<TexelBufferViewImpl*>(viewImplBase);
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

void ResourceCommandEncoderImpl::clearBuffer(IBuffer* buffer, const BufferRange* range)
{
    BufferImpl* bufferImpl = static_cast<BufferImpl*>(buffer);

    m_commandBuffer->requireBufferState(bufferImpl, ResourceState::CopyDestination);
    m_commandBuffer->commitBarriers();

    auto& api = m_commandBuffer->m_device->m_api;
    uint64_t offset = range ? range->offset : 0;
    uint64_t size = range ? range->size : bufferImpl->m_desc.size;
    api.vkCmdFillBuffer(m_commandBuffer->m_commandBuffer, bufferImpl->m_buffer.m_buffer, offset, size, 0);
}

void ResourceCommandEncoderImpl::clearTexture(
    ITexture* texture,
    const ClearValue& clearValue,
    const SubresourceRange* subresourceRange,
    bool clearDepth,
    bool clearStencil
)
{
    SLANG_RHI_UNIMPLEMENTED("clearTexture");
}

void ResourceCommandEncoderImpl::resolveQuery(
    IQueryPool* queryPool,
    GfxIndex index,
    GfxCount count,
    IBuffer* buffer,
    Offset offset
)
{
    QueryPoolImpl* poolImpl = static_cast<QueryPoolImpl*>(queryPool);
    BufferImpl* bufferImpl = static_cast<BufferImpl*>(buffer);

    m_commandBuffer->requireBufferState(bufferImpl, ResourceState::CopyDestination);
    m_commandBuffer->commitBarriers();

    auto& api = m_commandBuffer->m_device->m_api;
    api.vkCmdCopyQueryPoolResults(
        m_commandBuffer->m_commandBuffer,
        poolImpl->m_pool,
        index,
        count,
        bufferImpl->m_buffer.m_buffer,
        offset,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
    );
}

void ResourceCommandEncoderImpl::copyTextureToBuffer(
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

    BufferImpl* dstBuffer = static_cast<BufferImpl*>(dst);
    TextureImpl* srcTexture = static_cast<TextureImpl*>(src);

    m_commandBuffer->requireBufferState(dstBuffer, ResourceState::CopyDestination);
    m_commandBuffer->requireTextureState(srcTexture, srcSubresource, ResourceState::CopySource);
    m_commandBuffer->commitBarriers();

    VkBufferImageCopy region = {};
    region.bufferOffset = dstOffset;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VulkanUtil::getAspectMask(srcSubresource.aspectMask, srcTexture->m_vkformat);
    region.imageSubresource.mipLevel = srcSubresource.mipLevel;
    region.imageSubresource.baseArrayLayer = srcSubresource.baseArrayLayer;
    region.imageSubresource.layerCount = srcSubresource.layerCount;
    region.imageOffset = {(int32_t)srcOffset.x, (int32_t)srcOffset.y, (int32_t)srcOffset.z};
    region.imageExtent = {uint32_t(extent.width), uint32_t(extent.height), uint32_t(extent.depth)};

    auto& api = m_commandBuffer->m_device->m_api;
    api.vkCmdCopyImageToBuffer(
        m_commandBuffer->m_commandBuffer,
        srcTexture->m_image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dstBuffer->m_buffer.m_buffer,
        1,
        &region
    );
}

// RenderCommandEncoderImpl

Result RenderCommandEncoderImpl::beginPass(const RenderPassDesc& desc)
{
    auto& api = *m_api;

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
        TextureViewImpl* view = static_cast<TextureViewImpl*>(attachment.view);
        if (!view)
            return SLANG_FAIL;
        TextureViewImpl* resolveView = static_cast<TextureViewImpl*>(attachment.resolveTarget);

        m_renderTargetViews[i] = view;
        m_resolveTargetViews[i] = resolveView;

        // Transition state
        m_commandBuffer
            ->requireTextureState(view->m_texture, view->m_desc.subresourceRange, ResourceState::RenderTarget);
        if (resolveView)
            m_commandBuffer->requireTextureState(
                resolveView->m_texture,
                resolveView->m_desc.subresourceRange,
                ResourceState::ResolveDestination
            );

        // Determine render area
        const TextureViewDesc& viewDesc = view->m_desc;
        const TextureDesc& textureDesc = view->m_texture->m_desc;
        uint32_t width = getMipLevelSize(viewDesc.subresourceRange.mipLevel, textureDesc.size.width);
        uint32_t height = getMipLevelSize(viewDesc.subresourceRange.mipLevel, textureDesc.size.height);
        renderArea.extent.width = std::min(renderArea.extent.width, width);
        renderArea.extent.height = std::min(renderArea.extent.height, height);
        uint32_t attachmentLayerCount = (textureDesc.type == TextureType::Texture3D)
                                            ? textureDesc.size.depth
                                            : viewDesc.subresourceRange.layerCount;
        layerCount = std::max(layerCount, attachmentLayerCount);

        // Create attachment info
        VkRenderingAttachmentInfoKHR attachmentInfo = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
        attachmentInfo.imageView = static_cast<TextureViewImpl*>(attachment.view)->getView().imageView;
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
        TextureViewImpl* view = static_cast<TextureViewImpl*>(attachment.view);
        if (!view)
            return SLANG_FAIL;

        m_depthStencilView = static_cast<TextureViewImpl*>(desc.depthStencilAttachment->view);

        // Transition state
        m_commandBuffer->requireTextureState(
            view->m_texture,
            view->m_desc.subresourceRange,
            attachment.depthReadOnly ? ResourceState::DepthRead : ResourceState::DepthWrite
        );

        // Determine render area
        const TextureViewDesc& viewDesc = view->m_desc;
        const TextureDesc& textureDesc = view->m_texture->m_desc;
        uint32_t width = getMipLevelSize(viewDesc.subresourceRange.mipLevel, textureDesc.size.width);
        uint32_t height = getMipLevelSize(viewDesc.subresourceRange.mipLevel, textureDesc.size.height);
        renderArea.extent.width = std::min(renderArea.extent.width, width);
        renderArea.extent.height = std::min(renderArea.extent.height, height);

        // Create attachment info
        if (VulkanUtil::isDepthFormat(view->m_texture->m_vkformat))
        {
            hasDepthAttachment = true;
            const auto& attachment = *desc.depthStencilAttachment;
            depthAttachmentInfo.imageView = static_cast<TextureViewImpl*>(attachment.view)->getView().imageView;
            depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachmentInfo.loadOp = translateLoadOp(attachment.depthLoadOp);
            depthAttachmentInfo.storeOp = translateStoreOp(attachment.depthStoreOp);
            depthAttachmentInfo.clearValue.depthStencil.depth = attachment.depthClearValue;
        }
        if (VulkanUtil::isStencilFormat(view->m_texture->m_vkformat))
        {
            hasStencilAttachment = true;
            const auto& attachment = *desc.depthStencilAttachment;
            stencilAttachmentInfo.imageView = static_cast<TextureViewImpl*>(attachment.view)->getView().imageView;
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

    api.vkCmdBeginRenderingKHR(m_vkCommandBuffer, &renderingInfo);

    return SLANG_OK;
}

void RenderCommandEncoderImpl::endEncoding()
{
    auto& api = *m_api;
    api.vkCmdEndRenderingKHR(m_vkCommandBuffer);

    m_renderTargetViews.clear();
    m_resolveTargetViews.clear();
    m_depthStencilView = nullptr;

    endEncodingImpl();
}

Result RenderCommandEncoderImpl::bindPipeline(IPipeline* pipeline, IShaderObject** outRootObject)
{
    return setPipelineImpl(pipeline, outRootObject);
}

Result RenderCommandEncoderImpl::bindPipelineWithRootObject(IPipeline* pipeline, IShaderObject* rootObject)
{
    return setPipelineWithRootObjectImpl(pipeline, rootObject);
}

void RenderCommandEncoderImpl::setViewports(GfxCount count, const Viewport* viewports)
{
    static const int kMaxViewports = 8; // TODO: base on device caps
    SLANG_RHI_ASSERT(count <= kMaxViewports);

    m_viewports.resize(count);
    for (GfxIndex ii = 0; ii < count; ++ii)
    {
        auto& inViewport = viewports[ii];
        auto& vkViewport = m_viewports[ii];

        vkViewport.x = inViewport.originX;
        vkViewport.y = inViewport.originY + inViewport.extentY;
        vkViewport.width = inViewport.extentX;
        vkViewport.height = -inViewport.extentY;
        vkViewport.minDepth = inViewport.minZ;
        vkViewport.maxDepth = inViewport.maxZ;
    }

    auto& api = *m_api;
    api.vkCmdSetViewport(m_vkCommandBuffer, 0, uint32_t(count), m_viewports.data());
}

void RenderCommandEncoderImpl::setScissorRects(GfxCount count, const ScissorRect* rects)
{
    static const int kMaxScissorRects = 8; // TODO: base on device caps
    SLANG_RHI_ASSERT(count <= kMaxScissorRects);

    m_scissorRects.resize(count);
    for (GfxIndex ii = 0; ii < count; ++ii)
    {
        auto& inRect = rects[ii];
        auto& vkRect = m_scissorRects[ii];

        vkRect.offset.x = int32_t(inRect.minX);
        vkRect.offset.y = int32_t(inRect.minY);
        vkRect.extent.width = uint32_t(inRect.maxX - inRect.minX);
        vkRect.extent.height = uint32_t(inRect.maxY - inRect.minY);
    }

    auto& api = *m_api;
    api.vkCmdSetScissor(m_vkCommandBuffer, 0, uint32_t(m_scissorRects.size()), m_scissorRects.data());
}

void RenderCommandEncoderImpl::setVertexBuffers(
    GfxIndex startSlot,
    GfxCount slotCount,
    IBuffer* const* buffers,
    const Offset* offsets
)
{
    for (GfxIndex i = 0; i < GfxIndex(slotCount); i++)
    {
        GfxIndex slotIndex = startSlot + i;
        BufferImpl* buffer = static_cast<BufferImpl*>(buffers[i]);
        if (buffer)
        {
            VkBuffer vertexBuffers[] = {buffer->m_buffer.m_buffer};
            VkDeviceSize offset = VkDeviceSize(offsets[i]);

            m_api->vkCmdBindVertexBuffers(m_vkCommandBuffer, (uint32_t)slotIndex, 1, vertexBuffers, &offset);
        }
    }
}

void RenderCommandEncoderImpl::setIndexBuffer(IBuffer* buffer, IndexFormat indexFormat, Offset offset)
{
    VkIndexType indexType = VK_INDEX_TYPE_UINT16;
    switch (indexFormat)
    {
    case IndexFormat::UInt16:
        indexType = VK_INDEX_TYPE_UINT16;
        break;
    case IndexFormat::UInt32:
        indexType = VK_INDEX_TYPE_UINT32;
        break;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported index format");
    }

    BufferImpl* bufferImpl = static_cast<BufferImpl*>(buffer);

    m_api->vkCmdBindIndexBuffer(m_vkCommandBuffer, bufferImpl->m_buffer.m_buffer, (VkDeviceSize)offset, indexType);
}

Result RenderCommandEncoderImpl::prepareDraw()
{
    auto pipeline = static_cast<PipelineImpl*>(m_currentPipeline.Ptr());
    if (!pipeline)
    {
        return SLANG_FAIL;
    }
    SLANG_RETURN_ON_FAIL(bindRenderState(VK_PIPELINE_BIND_POINT_GRAPHICS));
    return SLANG_OK;
}

Result RenderCommandEncoderImpl::draw(GfxCount vertexCount, GfxIndex startVertex)
{
    SLANG_RETURN_ON_FAIL(prepareDraw());
    auto& api = *m_api;
    api.vkCmdDraw(m_vkCommandBuffer, vertexCount, 1, 0, 0);
    return SLANG_OK;
}

Result RenderCommandEncoderImpl::drawIndexed(GfxCount indexCount, GfxIndex startIndex, GfxIndex baseVertex)
{
    SLANG_RETURN_ON_FAIL(prepareDraw());
    auto& api = *m_api;
    api.vkCmdDrawIndexed(m_vkCommandBuffer, indexCount, 1, startIndex, baseVertex, 0);
    return SLANG_OK;
}

void RenderCommandEncoderImpl::setStencilReference(uint32_t referenceValue)
{
    auto& api = *m_api;
    api.vkCmdSetStencilReference(m_vkCommandBuffer, VK_STENCIL_FRONT_AND_BACK, referenceValue);
}

Result RenderCommandEncoderImpl::drawIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    // Vulkan does not support sourcing the count from a buffer.
    if (countBuffer)
        return SLANG_FAIL;

    SLANG_RETURN_ON_FAIL(prepareDraw());
    auto& api = *m_api;
    auto argBufferImpl = static_cast<BufferImpl*>(argBuffer);
    api.vkCmdDrawIndirect(
        m_vkCommandBuffer,
        argBufferImpl->m_buffer.m_buffer,
        argOffset,
        maxDrawCount,
        sizeof(VkDrawIndirectCommand)
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
    // Vulkan does not support sourcing the count from a buffer.
    if (countBuffer)
        return SLANG_FAIL;

    SLANG_RETURN_ON_FAIL(prepareDraw());

    auto& api = *m_api;
    auto argBufferImpl = static_cast<BufferImpl*>(argBuffer);
    api.vkCmdDrawIndexedIndirect(
        m_vkCommandBuffer,
        argBufferImpl->m_buffer.m_buffer,
        argOffset,
        maxDrawCount,
        sizeof(VkDrawIndexedIndirectCommand)
    );
    return SLANG_OK;
}

Result RenderCommandEncoderImpl::setSamplePositions(
    GfxCount samplesPerPixel,
    GfxCount pixelCount,
    const SamplePosition* samplePositions
)
{
    if (m_api->vkCmdSetSampleLocationsEXT)
    {
        VkSampleLocationsInfoEXT sampleLocInfo = {};
        sampleLocInfo.sType = VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT;
        sampleLocInfo.sampleLocationsCount = samplesPerPixel * pixelCount;
        sampleLocInfo.sampleLocationsPerPixel = (VkSampleCountFlagBits)samplesPerPixel;
        m_api->vkCmdSetSampleLocationsEXT(m_vkCommandBuffer, &sampleLocInfo);
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
    auto& api = *m_api;
    api.vkCmdDraw(m_vkCommandBuffer, vertexCount, instanceCount, startVertex, startInstanceLocation);
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
    auto& api = *m_api;
    api.vkCmdDrawIndexed(
        m_vkCommandBuffer,
        indexCount,
        instanceCount,
        startIndexLocation,
        baseVertexLocation,
        startInstanceLocation
    );
    return SLANG_OK;
}

Result RenderCommandEncoderImpl::drawMeshTasks(int x, int y, int z)
{
    SLANG_RETURN_ON_FAIL(prepareDraw());
    auto& api = *m_api;
    api.vkCmdDrawMeshTasksEXT(m_vkCommandBuffer, x, y, z);
    return SLANG_OK;
}

// ComputeCommandEncoderImpl

void ComputeCommandEncoderImpl::endEncoding()
{
    endEncodingImpl();
}

Result ComputeCommandEncoderImpl::bindPipeline(IPipeline* pipeline, IShaderObject** outRootObject)
{
    return setPipelineImpl(pipeline, outRootObject);
}

Result ComputeCommandEncoderImpl::bindPipelineWithRootObject(IPipeline* pipeline, IShaderObject* rootObject)
{
    return setPipelineWithRootObjectImpl(pipeline, rootObject);
}

Result ComputeCommandEncoderImpl::dispatchCompute(int x, int y, int z)
{
    auto pipeline = static_cast<PipelineImpl*>(m_currentPipeline.Ptr());
    if (!pipeline)
    {
        return SLANG_FAIL;
    }

    // Also create descriptor sets based on the given pipeline layout
    SLANG_RETURN_ON_FAIL(bindRenderState(VK_PIPELINE_BIND_POINT_COMPUTE));
    m_api->vkCmdDispatch(m_vkCommandBuffer, x, y, z);
    return SLANG_OK;
}

Result ComputeCommandEncoderImpl::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    SLANG_RHI_UNIMPLEMENTED("dispatchComputeIndirect");
}

// RayTracingCommandEncoderImpl

void RayTracingCommandEncoderImpl::_memoryBarrier(
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
        memBarriers[i].srcQueueFamilyIndex = m_commandBuffer->m_device->m_queueFamilyIndex;
        memBarriers[i].dstQueueFamilyIndex = m_commandBuffer->m_device->m_queueFamilyIndex;

        auto asImpl = static_cast<AccelerationStructureImpl*>(structures[i]);
        memBarriers[i].buffer = asImpl->m_buffer->m_buffer.m_buffer;
        memBarriers[i].offset = asImpl->m_offset;
        memBarriers[i].size = asImpl->m_size;
    }
    m_commandBuffer->m_device->m_api.vkCmdPipelineBarrier(
        m_commandBuffer->m_commandBuffer,
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

void RayTracingCommandEncoderImpl::_queryAccelerationStructureProperties(
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
        vkHandles[i] = static_cast<AccelerationStructureImpl*>(accelerationStructures[i])->m_vkHandle;
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
            getDebugCallback()->handleMessage(
                DebugMessageType::Error,
                DebugMessageSource::Layer,
                "Invalid query type for use in queryAccelerationStructureProperties."
            );
            return;
        }
        auto queryPool = static_cast<QueryPoolImpl*>(queryDescs[i].queryPool)->m_pool;
        m_commandBuffer->m_device->m_api.vkCmdResetQueryPool(
            m_commandBuffer->m_commandBuffer,
            queryPool,
            (uint32_t)queryDescs[i].firstQueryIndex,
            1
        );
        m_commandBuffer->m_device->m_api.vkCmdWriteAccelerationStructuresPropertiesKHR(
            m_commandBuffer->m_commandBuffer,
            accelerationStructureCount,
            vkHandles.data(),
            queryType,
            queryPool,
            queryDescs[i].firstQueryIndex
        );
    }
}

void RayTracingCommandEncoderImpl::buildAccelerationStructure(
    const IAccelerationStructure::BuildDesc& desc,
    GfxCount propertyQueryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    AccelerationStructureBuildGeometryInfoBuilder geomInfoBuilder;
    if (geomInfoBuilder.build(desc.inputs, getDebugCallback()) != SLANG_OK)
        return;

    if (desc.dest)
    {
        geomInfoBuilder.buildInfo.dstAccelerationStructure =
            static_cast<AccelerationStructureImpl*>(desc.dest)->m_vkHandle;
    }
    if (desc.source)
    {
        geomInfoBuilder.buildInfo.srcAccelerationStructure =
            static_cast<AccelerationStructureImpl*>(desc.source)->m_vkHandle;
    }
    geomInfoBuilder.buildInfo.scratchData.deviceAddress = desc.scratchData;

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
    m_commandBuffer->m_device->m_api.vkCmdBuildAccelerationStructuresKHR(
        m_commandBuffer->m_commandBuffer,
        1,
        &geomInfoBuilder.buildInfo,
        &rangeInfoPtr
    );

    if (propertyQueryCount)
    {
        _memoryBarrier(1, &desc.dest, AccessFlag::Write, AccessFlag::Read);
        _queryAccelerationStructureProperties(1, &desc.dest, propertyQueryCount, queryDescs);
    }
}

void RayTracingCommandEncoderImpl::copyAccelerationStructure(
    IAccelerationStructure* dest,
    IAccelerationStructure* src,
    AccelerationStructureCopyMode mode
)
{
    VkCopyAccelerationStructureInfoKHR copyInfo = {VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
    copyInfo.src = static_cast<AccelerationStructureImpl*>(src)->m_vkHandle;
    copyInfo.dst = static_cast<AccelerationStructureImpl*>(dest)->m_vkHandle;
    switch (mode)
    {
    case AccelerationStructureCopyMode::Clone:
        copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR;
        break;
    case AccelerationStructureCopyMode::Compact:
        copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
        break;
    default:
        getDebugCallback()->handleMessage(
            DebugMessageType::Error,
            DebugMessageSource::Layer,
            "Unsupported AccelerationStructureCopyMode."
        );
        return;
    }
    m_commandBuffer->m_device->m_api.vkCmdCopyAccelerationStructureKHR(m_commandBuffer->m_commandBuffer, &copyInfo);
}

void RayTracingCommandEncoderImpl::queryAccelerationStructureProperties(
    GfxCount accelerationStructureCount,
    IAccelerationStructure* const* accelerationStructures,
    GfxCount queryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    _queryAccelerationStructureProperties(accelerationStructureCount, accelerationStructures, queryCount, queryDescs);
}

void RayTracingCommandEncoderImpl::serializeAccelerationStructure(DeviceAddress dest, IAccelerationStructure* source)
{
    VkCopyAccelerationStructureToMemoryInfoKHR copyInfo = {
        VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_TO_MEMORY_INFO_KHR
    };
    copyInfo.src = static_cast<AccelerationStructureImpl*>(source)->m_vkHandle;
    copyInfo.dst.deviceAddress = dest;
    copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_SERIALIZE_KHR;
    m_commandBuffer->m_device->m_api.vkCmdCopyAccelerationStructureToMemoryKHR(
        m_commandBuffer->m_commandBuffer,
        &copyInfo
    );
}

void RayTracingCommandEncoderImpl::deserializeAccelerationStructure(IAccelerationStructure* dest, DeviceAddress source)
{
    VkCopyMemoryToAccelerationStructureInfoKHR copyInfo = {
        VK_STRUCTURE_TYPE_COPY_MEMORY_TO_ACCELERATION_STRUCTURE_INFO_KHR
    };
    copyInfo.src.deviceAddress = source;
    copyInfo.dst = static_cast<AccelerationStructureImpl*>(dest)->m_vkHandle;
    copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_DESERIALIZE_KHR;
    m_commandBuffer->m_device->m_api.vkCmdCopyMemoryToAccelerationStructureKHR(
        m_commandBuffer->m_commandBuffer,
        &copyInfo
    );
}

Result RayTracingCommandEncoderImpl::bindPipeline(IPipeline* pipeline, IShaderObject** outRootObject)
{
    return setPipelineImpl(pipeline, outRootObject);
}

Result RayTracingCommandEncoderImpl::bindPipelineWithRootObject(IPipeline* pipeline, IShaderObject* rootObject)
{
    return setPipelineWithRootObjectImpl(pipeline, rootObject);
}

Result RayTracingCommandEncoderImpl::dispatchRays(
    GfxIndex raygenShaderIndex,
    IShaderTable* shaderTable,
    GfxCount width,
    GfxCount height,
    GfxCount depth
)
{
    auto api = m_commandBuffer->m_device->m_api;
    auto vkCommandBuffer = m_commandBuffer->m_commandBuffer;

    SLANG_RETURN_ON_FAIL(bindRenderState(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR));

    auto rtProps = api.m_rtProperties;
    auto shaderTableImpl = (ShaderTableImpl*)shaderTable;
    auto alignedHandleSize = VulkanUtil::calcAligned(rtProps.shaderGroupHandleSize, rtProps.shaderGroupHandleAlignment);

    auto shaderTableBuffer =
        shaderTableImpl->getOrCreateBuffer(m_currentPipeline, m_commandBuffer->m_transientHeap, this);
    auto shaderTableAddr = shaderTableBuffer->getDeviceAddress();

    VkStridedDeviceAddressRegionKHR raygenSBT;
    raygenSBT.stride = VulkanUtil::calcAligned(alignedHandleSize, rtProps.shaderGroupBaseAlignment);
    raygenSBT.deviceAddress = shaderTableAddr + raygenShaderIndex * raygenSBT.stride;
    raygenSBT.size = raygenSBT.stride;

    VkStridedDeviceAddressRegionKHR missSBT;
    missSBT.deviceAddress = shaderTableAddr + shaderTableImpl->m_raygenTableSize;
    missSBT.stride = alignedHandleSize;
    missSBT.size = shaderTableImpl->m_missTableSize;

    VkStridedDeviceAddressRegionKHR hitSBT;
    hitSBT.deviceAddress = missSBT.deviceAddress + missSBT.size;
    hitSBT.stride = alignedHandleSize;
    hitSBT.size = shaderTableImpl->m_hitTableSize;

    VkStridedDeviceAddressRegionKHR callableSBT;
    callableSBT.deviceAddress = hitSBT.deviceAddress + hitSBT.size;
    callableSBT.stride = alignedHandleSize;
    callableSBT.size = shaderTableImpl->m_callableTableSize;

    api.vkCmdTraceRaysKHR(
        vkCommandBuffer,
        &raygenSBT,
        &missSBT,
        &hitSBT,
        &callableSBT,
        (uint32_t)width,
        (uint32_t)height,
        (uint32_t)depth
    );

    return SLANG_OK;
}

void RayTracingCommandEncoderImpl::endEncoding()
{
    endEncodingImpl();
}

} // namespace rhi::vk
