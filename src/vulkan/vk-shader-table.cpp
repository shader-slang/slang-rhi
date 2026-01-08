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

    auto writeTableEntry = [&](void* dest, const std::string& name, const ShaderRecordOverwrite* overwrite)
    {
        auto it = pipeline->m_shaderGroupIndexByName.find(name);
        if (it != pipeline->m_shaderGroupIndexByName.end())
        {
            auto src = handles.data() + it->second * handleSize;
            memcpy(dest, src, handleSize);
        }
        if (overwrite && overwrite->size > 0)
        {
            memcpy((uint8_t*)dest + overwrite->offset, overwrite->data, overwrite->size);
        }
    };

    auto tableData = std::make_unique<uint8_t[]>(tableSize);
    uint8_t* tablePtr = tableData.get();
    memset(tableData.get(), 0, tableSize);

    for (uint32_t i = 0; i < m_rayGenShaderCount; i++)
    {
        writeTableEntry(
            tablePtr + i * raygenRecordSize,
            m_rayGenShaderEntryPointNames[i],
            i < m_rayGenRecordOverwrites.size() ? &m_rayGenRecordOverwrites[i] : nullptr
        );
    }
    tablePtr += m_raygenTableSize;

    for (uint32_t i = 0; i < m_missShaderCount; i++)
    {
        writeTableEntry(
            tablePtr + i * missRecordSize,
            m_missShaderEntryPointNames[i],
            i < m_missRecordOverwrites.size() ? &m_missRecordOverwrites[i] : nullptr
        );
    }
    tablePtr += m_missTableSize;

    for (uint32_t i = 0; i < m_hitGroupCount; i++)
    {
        writeTableEntry(
            tablePtr + i * hitGroupRecordSize,
            m_hitGroupNames[i],
            i < m_hitGroupRecordOverwrites.size() ? &m_hitGroupRecordOverwrites[i] : nullptr
        );
    }
    tablePtr += m_hitTableSize;

    for (uint32_t i = 0; i < m_callableShaderCount; i++)
    {
        writeTableEntry(
            tablePtr + i * callableRecordSize,
            m_callableShaderEntryPointNames[i],
            i < m_callableRecordOverwrites.size() ? &m_callableRecordOverwrites[i] : nullptr
        );
    }

    ComPtr<IBuffer> buffer;
    BufferDesc bufferDesc = {};
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    bufferDesc.usage = BufferUsage::ShaderTable | BufferUsage::CopyDestination;
    bufferDesc.defaultState = ResourceState::General;
    bufferDesc.size = tableSize;
    device->createBuffer(bufferDesc, tableData.get(), buffer.writeRef());

    // Vulkan should always align allocations to the required minimum (by spec).
    // However, it seems with lavapipe this is not always the case.
    SLANG_RHI_ASSERT(buffer->getDeviceAddress() % rtpProps.shaderGroupBaseAlignment == 0);

    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer.get());
    m_buffers.emplace(pipeline, bufferImpl);
    return bufferImpl;
}

} // namespace rhi::vk
