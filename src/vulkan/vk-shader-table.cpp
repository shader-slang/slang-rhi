#include "vk-shader-table.h"
#include "vk-device.h"
#include "vk-helper-functions.h"
#include "vk-transient-heap.h"

#include <vector>

namespace rhi::vk {

RefPtr<Buffer> ShaderTableImpl::createDeviceBuffer(
    Pipeline* pipeline,
    TransientResourceHeap* transientHeap,
    IRayTracingPassEncoder* encoder
)
{
    auto vkApi = m_device->m_api;
    auto rtProps = vkApi.m_rtProperties;
    uint32_t handleSize = rtProps.shaderGroupHandleSize;
    m_raygenTableSize = m_rayGenShaderCount * rtProps.shaderGroupBaseAlignment;
    m_missTableSize =
        (uint32_t)VulkanUtil::calcAligned(m_missShaderCount * handleSize, rtProps.shaderGroupBaseAlignment);
    m_hitTableSize = (uint32_t)VulkanUtil::calcAligned(m_hitGroupCount * handleSize, rtProps.shaderGroupBaseAlignment);
    m_callableTableSize =
        (uint32_t)VulkanUtil::calcAligned(m_callableShaderCount * handleSize, rtProps.shaderGroupBaseAlignment);
    uint32_t tableSize = m_raygenTableSize + m_missTableSize + m_hitTableSize + m_callableTableSize;

    auto pipelineImpl = static_cast<RayTracingPipelineImpl*>(pipeline);
    ComPtr<IBuffer> buffer;
    BufferDesc bufferDesc = {};
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    bufferDesc.usage = BufferUsage::ShaderTable | BufferUsage::CopyDestination;
    bufferDesc.defaultState = ResourceState::General;
    bufferDesc.size = tableSize;
    static_cast<vk::DeviceImpl*>(m_device)->createBuffer(bufferDesc, nullptr, buffer.writeRef());

    TransientResourceHeapImpl* transientHeapImpl = static_cast<TransientResourceHeapImpl*>(transientHeap);

    IBuffer* stagingBuffer = nullptr;
    Offset stagingBufferOffset = 0;
    transientHeapImpl->allocateStagingBuffer(tableSize, stagingBuffer, stagingBufferOffset, MemoryType::Upload);

    SLANG_RHI_ASSERT(stagingBuffer);
    void* stagingPtr = nullptr;
    stagingBuffer->map(nullptr, &stagingPtr);

    std::vector<uint8_t> handles;
    auto handleCount = pipelineImpl->shaderGroupCount;
    auto totalHandleSize = handleSize * handleCount;
    handles.resize(totalHandleSize);
    auto result = vkApi.vkGetRayTracingShaderGroupHandlesKHR(
        m_device->m_device,
        pipelineImpl->m_pipeline,
        0,
        (uint32_t)handleCount,
        totalHandleSize,
        handles.data()
    );

    uint8_t* stagingBufferPtr = (uint8_t*)stagingPtr + stagingBufferOffset;
    auto subTablePtr = stagingBufferPtr;
    Int shaderTableEntryCounter = 0;

    // Each loop calculates the copy source and destination locations by fetching the name
    // of the shader group from the list of shader group names and getting its corresponding
    // index in the buffer of handles.
    for (uint32_t i = 0; i < m_rayGenShaderCount; i++)
    {
        auto dstHandlePtr = subTablePtr + i * rtProps.shaderGroupBaseAlignment;
        auto shaderGroupName = m_shaderGroupNames[shaderTableEntryCounter++];
        auto it = pipelineImpl->shaderGroupNameToIndex.find(shaderGroupName);
        if (it == pipelineImpl->shaderGroupNameToIndex.end())
            continue;
        auto shaderGroupIndex = it->second;
        auto srcHandlePtr = handles.data() + shaderGroupIndex * handleSize;
        memcpy(dstHandlePtr, srcHandlePtr, handleSize);
        memset(dstHandlePtr + handleSize, 0, rtProps.shaderGroupBaseAlignment - handleSize);
    }
    subTablePtr += m_raygenTableSize;

    for (uint32_t i = 0; i < m_missShaderCount; i++)
    {
        auto dstHandlePtr = subTablePtr + i * handleSize;
        auto shaderGroupName = m_shaderGroupNames[shaderTableEntryCounter++];
        auto it = pipelineImpl->shaderGroupNameToIndex.find(shaderGroupName);
        if (it == pipelineImpl->shaderGroupNameToIndex.end())
            continue;
        auto shaderGroupIndex = it->second;
        auto srcHandlePtr = handles.data() + shaderGroupIndex * handleSize;
        memcpy(dstHandlePtr, srcHandlePtr, handleSize);
    }
    subTablePtr += m_missTableSize;

    for (uint32_t i = 0; i < m_hitGroupCount; i++)
    {
        auto dstHandlePtr = subTablePtr + i * handleSize;
        auto shaderGroupName = m_shaderGroupNames[shaderTableEntryCounter++];
        auto it = pipelineImpl->shaderGroupNameToIndex.find(shaderGroupName);
        if (it == pipelineImpl->shaderGroupNameToIndex.end())
            continue;
        auto shaderGroupIndex = it->second;
        auto srcHandlePtr = handles.data() + shaderGroupIndex * handleSize;
        memcpy(dstHandlePtr, srcHandlePtr, handleSize);
    }
    subTablePtr += m_hitTableSize;

    for (uint32_t i = 0; i < m_callableShaderCount; i++)
    {
        auto dstHandlePtr = subTablePtr + i * handleSize;
        auto shaderGroupName = m_shaderGroupNames[shaderTableEntryCounter++];
        auto it = pipelineImpl->shaderGroupNameToIndex.find(shaderGroupName);
        if (it == pipelineImpl->shaderGroupNameToIndex.end())
            continue;
        auto shaderGroupIndex = it->second;
        auto srcHandlePtr = handles.data() + shaderGroupIndex * handleSize;
        memcpy(dstHandlePtr, srcHandlePtr, handleSize);
    }
    subTablePtr += m_callableTableSize;

    stagingBuffer->unmap(nullptr);

    VkBufferCopy copyRegion;
    copyRegion.dstOffset = 0;
    copyRegion.srcOffset = stagingBufferOffset;
    copyRegion.size = tableSize;
    vkApi.vkCmdCopyBuffer(
        static_cast<RayTracingPassEncoderImpl*>(encoder)->m_commandBuffer->m_commandBuffer,
        static_cast<BufferImpl*>(stagingBuffer)->m_buffer.m_buffer,
        static_cast<BufferImpl*>(buffer.get())->m_buffer.m_buffer,
        /* regionCount: */ 1,
        &copyRegion
    );
    encoder->setBufferState(buffer, ResourceState::ShaderResource);
    RefPtr<Buffer> resultPtr = static_cast<Buffer*>(buffer.get());
    return _Move(resultPtr);
}

} // namespace rhi::vk
