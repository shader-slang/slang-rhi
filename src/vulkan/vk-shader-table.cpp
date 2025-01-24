#include "vk-shader-table.h"
#include "vk-device.h"
#include "vk-helper-functions.h"
#include "vk-pipeline.h"
#include "vk-command.h"

#include <vector>

namespace rhi::vk {

RefPtr<Buffer> ShaderTableImpl::createDeviceBuffer(RayTracingPipeline* pipeline)
{
    auto& api = m_device->m_api;
    const auto& rtProps = api.m_rtProperties;
    uint32_t handleSize = rtProps.shaderGroupHandleSize;
    m_raygenTableSize = m_rayGenShaderCount * rtProps.shaderGroupBaseAlignment;
    m_missTableSize = (uint32_t)math::calcAligned(m_missShaderCount * handleSize, rtProps.shaderGroupBaseAlignment);
    m_hitTableSize = (uint32_t)math::calcAligned(m_hitGroupCount * handleSize, rtProps.shaderGroupBaseAlignment);
    m_callableTableSize =
        (uint32_t)math::calcAligned(m_callableShaderCount * handleSize, rtProps.shaderGroupBaseAlignment);
    uint32_t tableSize = m_raygenTableSize + m_missTableSize + m_hitTableSize + m_callableTableSize;

    auto pipelineImpl = checked_cast<RayTracingPipelineImpl*>(pipeline);
    auto tableData = std::make_unique<uint8_t[]>(tableSize);

    std::vector<uint8_t> handles;
    auto handleCount = pipelineImpl->m_shaderGroupCount;
    auto totalHandleSize = handleSize * handleCount;
    handles.resize(totalHandleSize);
    auto result = api.vkGetRayTracingShaderGroupHandlesKHR(
        m_device->m_device,
        pipelineImpl->m_pipeline,
        0,
        (uint32_t)handleCount,
        totalHandleSize,
        handles.data()
    );

    uint8_t* subTablePtr = tableData.get();
    uint32_t shaderTableEntryCounter = 0;

    // Each loop calculates the copy source and destination locations by fetching the name
    // of the shader group from the list of shader group names and getting its corresponding
    // index in the buffer of handles.
    for (uint32_t i = 0; i < m_rayGenShaderCount; i++)
    {
        auto dstHandlePtr = subTablePtr + i * rtProps.shaderGroupBaseAlignment;
        auto shaderGroupName = m_shaderGroupNames[shaderTableEntryCounter++];
        auto it = pipelineImpl->m_shaderGroupNameToIndex.find(shaderGroupName);
        if (it == pipelineImpl->m_shaderGroupNameToIndex.end())
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
        auto it = pipelineImpl->m_shaderGroupNameToIndex.find(shaderGroupName);
        if (it == pipelineImpl->m_shaderGroupNameToIndex.end())
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
        auto it = pipelineImpl->m_shaderGroupNameToIndex.find(shaderGroupName);
        if (it == pipelineImpl->m_shaderGroupNameToIndex.end())
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
        auto it = pipelineImpl->m_shaderGroupNameToIndex.find(shaderGroupName);
        if (it == pipelineImpl->m_shaderGroupNameToIndex.end())
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
    m_device->createBuffer(bufferDesc, tableData.get(), buffer.writeRef());

    RefPtr<Buffer> resultPtr = checked_cast<Buffer*>(buffer.get());
    return std::move(resultPtr);
}

} // namespace rhi::vk
