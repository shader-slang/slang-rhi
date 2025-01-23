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

    void writeBuffer(BufferImpl* buffer, size_t offset, size_t size, const void* data) override
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


void CommandEncoderImpl::buildAccelerationStructure(
    const AccelerationStructureBuildDesc& desc,
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    BufferWithOffset scratchBuffer,
    uint32_t propertyQueryCount,
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
    for (size_t i = 0; i < geomInfoBuilder.primitiveCounts.size(); i++)
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


void CommandEncoderImpl::queryAccelerationStructureProperties(
    GfxCount accelerationStructureCount,
    IAccelerationStructure* const* accelerationStructures,
    GfxCount queryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    _queryAccelerationStructureProperties(accelerationStructureCount, accelerationStructures, queryCount, queryDescs);
}

void CommandEncoderImpl::writeTimestamp(IQueryPool* pool, GfxIndex index)
{
    _writeTimestamp(&m_device->m_api, m_cmdBuffer, pool, index);
}

Result CommandEncoderImpl::finish(ICommandBuffer** outCommandBuffer)
{
    if (!m_commandBuffer)
    {
        return SLANG_FAIL;
    }

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
    returnComPtr(outCommandBuffer, m_commandBuffer);
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

void CommandEncoderImpl::commitBarriers() {}


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
        m_device->m_api.vkCmdResetQueryPool(m_cmdBuffer, queryPool, queryDescs[i].firstQueryIndex, 1);
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
    context.transientHeap = m_transientHeap;
    context.encoder = this;
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
