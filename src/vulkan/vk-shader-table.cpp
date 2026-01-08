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

    // Calculate record sizes (without alignment).
    uint32_t raygenRecordSize = max(handleSize, m_rayGenRecordOverwriteMaxSize);
    uint32_t missRecordSize = max(handleSize, m_missRecordOverwriteMaxSize);
    uint32_t hitGroupRecordSize = max(handleSize, m_hitGroupRecordOverwriteMaxSize);
    uint32_t callableRecordSize = max(handleSize, m_callableRecordOverwriteMaxSize);

    // Align all record sizes to shaderGroupBaseAlignment.
    raygenRecordSize = (uint32_t)math::calcAligned2(raygenRecordSize, rtpProps.shaderGroupBaseAlignment);
    missRecordSize = (uint32_t)math::calcAligned2(missRecordSize, rtpProps.shaderGroupBaseAlignment);
    hitGroupRecordSize = (uint32_t)math::calcAligned2(hitGroupRecordSize, rtpProps.shaderGroupBaseAlignment);
    callableRecordSize = (uint32_t)math::calcAligned2(callableRecordSize, rtpProps.shaderGroupBaseAlignment);

    // Store strides for use when dispatching rays.
    m_raygenRecordStride = raygenRecordSize;
    m_missRecordStride = missRecordSize;
    m_hitGroupRecordStride = hitGroupRecordSize;
    m_callableRecordStride = callableRecordSize;

    m_raygenTableSize = m_rayGenShaderCount * raygenRecordSize;
    m_missTableSize = m_missShaderCount * missRecordSize;
    m_hitTableSize = m_hitGroupCount * hitGroupRecordSize;
    m_callableTableSize = m_callableShaderCount * callableRecordSize;
    uint32_t tableSize = m_raygenTableSize + m_missTableSize + m_hitTableSize + m_callableTableSize;

    auto tableData = std::make_unique<uint8_t[]>(tableSize);
    memset(tableData.get(), 0, tableSize);

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

    // Raygen records
    for (uint32_t i = 0; i < m_rayGenShaderCount; i++)
    {
        auto dstHandlePtr = subTablePtr + i * raygenRecordSize;
        auto shaderGroupName = m_shaderGroupNames[shaderTableEntryCounter++];
        auto it = pipeline->m_shaderGroupNameToIndex.find(shaderGroupName);
        if (it == pipeline->m_shaderGroupNameToIndex.end())
        {
            continue;
        }
        auto shaderGroupIndex = it->second;
        auto srcHandlePtr = handles.data() + shaderGroupIndex * handleSize;
        memcpy(dstHandlePtr, srcHandlePtr, handleSize);

        // Apply shader record overwrite if present
        if (i < m_rayGenRecordOverwrites.size())
        {
            const ShaderRecordOverwrite& overwrite = m_rayGenRecordOverwrites[i];
            if (overwrite.size > 0)
                memcpy(dstHandlePtr + overwrite.offset, overwrite.data, overwrite.size);
        }
    }
    subTablePtr += m_raygenTableSize;

    // Miss records
    for (uint32_t i = 0; i < m_missShaderCount; i++)
    {
        auto dstHandlePtr = subTablePtr + i * missRecordSize;
        auto shaderGroupName = m_shaderGroupNames[shaderTableEntryCounter++];
        auto it = pipeline->m_shaderGroupNameToIndex.find(shaderGroupName);
        if (it == pipeline->m_shaderGroupNameToIndex.end())
        {
            continue;
        }
        auto shaderGroupIndex = it->second;
        auto srcHandlePtr = handles.data() + shaderGroupIndex * handleSize;
        memcpy(dstHandlePtr, srcHandlePtr, handleSize);

        // Apply shader record overwrite if present
        if (i < m_missRecordOverwrites.size())
        {
            const ShaderRecordOverwrite& overwrite = m_missRecordOverwrites[i];
            if (overwrite.size > 0)
                memcpy(dstHandlePtr + overwrite.offset, overwrite.data, overwrite.size);
        }
    }
    subTablePtr += m_missTableSize;

    // Hit group records
    for (uint32_t i = 0; i < m_hitGroupCount; i++)
    {
        auto dstHandlePtr = subTablePtr + i * hitGroupRecordSize;
        auto shaderGroupName = m_shaderGroupNames[shaderTableEntryCounter++];
        auto it = pipeline->m_shaderGroupNameToIndex.find(shaderGroupName);
        if (it == pipeline->m_shaderGroupNameToIndex.end())
        {
            continue;
        }
        auto shaderGroupIndex = it->second;
        auto srcHandlePtr = handles.data() + shaderGroupIndex * handleSize;
        memcpy(dstHandlePtr, srcHandlePtr, handleSize);

        // Apply shader record overwrite if present
        if (i < m_hitGroupRecordOverwrites.size())
        {
            const ShaderRecordOverwrite& overwrite = m_hitGroupRecordOverwrites[i];
            if (overwrite.size > 0)
                memcpy(dstHandlePtr + overwrite.offset, overwrite.data, overwrite.size);
        }
    }
    subTablePtr += m_hitTableSize;

    // Callable records
    for (uint32_t i = 0; i < m_callableShaderCount; i++)
    {
        auto dstHandlePtr = subTablePtr + i * callableRecordSize;
        auto shaderGroupName = m_shaderGroupNames[shaderTableEntryCounter++];
        auto it = pipeline->m_shaderGroupNameToIndex.find(shaderGroupName);
        if (it == pipeline->m_shaderGroupNameToIndex.end())
        {
            continue;
        }
        auto shaderGroupIndex = it->second;
        auto srcHandlePtr = handles.data() + shaderGroupIndex * handleSize;
        memcpy(dstHandlePtr, srcHandlePtr, handleSize);

        // Apply shader record overwrite if present
        if (i < m_callableRecordOverwrites.size())
        {
            const ShaderRecordOverwrite& overwrite = m_callableRecordOverwrites[i];
            if (overwrite.size > 0)
                memcpy(dstHandlePtr + overwrite.offset, overwrite.data, overwrite.size);
        }
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
