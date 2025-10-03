#include "vk-shader-table.h"
#include "vk-device.h"
#include "vk-buffer.h"
#include "vk-pipeline.h"
#include "vk-command.h"

#include <vector>

namespace rhi::vk {

ShaderTableImpl::ShaderTableImpl(Device* device, const ShaderTableDesc& desc)
    : ShaderTable(device, desc)
{
}

BufferImpl* ShaderTableImpl::getBuffer(RayTracingPipelineImpl* pipeline)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    DeviceImpl* device = getDevice<DeviceImpl>();

    auto bufferIt = m_buffers.find(pipeline);
    if (bufferIt != m_buffers.end())
        return bufferIt->second.get();

    auto& api = device->m_api;
    const auto& rtpProps = api.m_rayTracingPipelineProperties;
    uint32_t handleSize = rtpProps.shaderGroupHandleSize;
    m_raygenTableSize = m_rayGenShaderCount * rtpProps.shaderGroupBaseAlignment;
    m_missTableSize = (uint32_t)math::calcAligned2(m_missShaderCount * handleSize, rtpProps.shaderGroupBaseAlignment);
    m_hitTableSize = (uint32_t)math::calcAligned2(m_hitGroupCount * handleSize, rtpProps.shaderGroupBaseAlignment);
    m_callableTableSize =
        (uint32_t)math::calcAligned2(m_callableShaderCount * handleSize, rtpProps.shaderGroupBaseAlignment);
    uint32_t tableSize = m_raygenTableSize + m_missTableSize + m_hitTableSize + m_callableTableSize;

    auto tableData = std::make_unique<uint8_t[]>(tableSize);

    std::vector<uint8_t> handles;
    auto handleCount = pipeline->m_shaderGroupCount;
    auto totalHandleSize = handleSize * handleCount;
    handles.resize(totalHandleSize);
    auto result = api.vkGetRayTracingShaderGroupHandlesKHR(
        device->m_device,
        pipeline->m_pipeline,
        0,
        (uint32_t)handleCount,
        totalHandleSize,
        handles.data()
    );
    SLANG_RHI_ASSERT(result == VK_SUCCESS);

    uint8_t* subTablePtr = tableData.get();
    uint32_t shaderTableEntryCounter = 0;

    // Each loop calculates the copy source and destination locations by fetching the name
    // of the shader group from the list of shader group names and getting its corresponding
    // index in the buffer of handles.
    for (uint32_t i = 0; i < m_rayGenShaderCount; i++)
    {
        auto dstHandlePtr = subTablePtr + i * rtpProps.shaderGroupBaseAlignment;
        auto shaderGroupName = m_shaderGroupNames[shaderTableEntryCounter++];
        auto it = pipeline->m_shaderGroupNameToIndex.find(shaderGroupName);
        if (it == pipeline->m_shaderGroupNameToIndex.end())
            continue;
        auto shaderGroupIndex = it->second;
        auto srcHandlePtr = handles.data() + shaderGroupIndex * handleSize;
        memcpy(dstHandlePtr, srcHandlePtr, handleSize);
        memset(dstHandlePtr + handleSize, 0, rtpProps.shaderGroupBaseAlignment - handleSize);
    }
    subTablePtr += m_raygenTableSize;

    for (uint32_t i = 0; i < m_missShaderCount; i++)
    {
        auto dstHandlePtr = subTablePtr + i * handleSize;
        auto shaderGroupName = m_shaderGroupNames[shaderTableEntryCounter++];
        auto it = pipeline->m_shaderGroupNameToIndex.find(shaderGroupName);
        if (it == pipeline->m_shaderGroupNameToIndex.end())
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
        auto it = pipeline->m_shaderGroupNameToIndex.find(shaderGroupName);
        if (it == pipeline->m_shaderGroupNameToIndex.end())
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
        auto it = pipeline->m_shaderGroupNameToIndex.find(shaderGroupName);
        if (it == pipeline->m_shaderGroupNameToIndex.end())
            continue;
        auto shaderGroupIndex = it->second;
        auto srcHandlePtr = handles.data() + shaderGroupIndex * handleSize;
        memcpy(dstHandlePtr, srcHandlePtr, handleSize);
    }
    subTablePtr += m_callableTableSize;

    ComPtr<IBuffer> buffer;
    BufferDesc bufferDesc = {};
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    bufferDesc.usage = BufferUsage::ShaderTable | BufferUsage::CopyDestination;
    bufferDesc.defaultState = ResourceState::General;
    bufferDesc.size = tableSize;
    device->createBuffer(bufferDesc, tableData.get(), buffer.writeRef());

    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer.get());
    m_buffers.emplace(pipeline, bufferImpl);
    return bufferImpl;
}

} // namespace rhi::vk
