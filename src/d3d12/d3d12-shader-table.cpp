#include "d3d12-shader-table.h"
#include "d3d12-device.h"
#include "d3d12-buffer.h"
#include "d3d12-pipeline.h"

#include "core/string.h"

namespace rhi::d3d12 {

ShaderTableImpl::ShaderTableImpl(Device* device, const ShaderTableDesc& desc)
    : ShaderTable(device, desc)
{
}

BufferImpl* ShaderTableImpl::getBuffer(RayTracingPipelineImpl* pipeline)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto bufferIt = m_buffers.find(pipeline);
    if (bufferIt != m_buffers.end())
        return bufferIt->second.get();

    // Calculate record sizes (without alignment).
    uint32_t raygenRecordSize = max(uint32_t(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES), m_rayGenRecordOverwriteMaxSize);
    uint32_t missRecordSize = max(uint32_t(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES), m_missRecordOverwriteMaxSize);
    uint32_t hitGroupRecordSize =
        max(uint32_t(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES), m_hitGroupRecordOverwriteMaxSize);
    uint32_t callableRecordSize =
        max(uint32_t(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES), m_callableRecordOverwriteMaxSize);

    // Align all record sizes to D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT.
    raygenRecordSize = (uint32_t)math::calcAligned2(raygenRecordSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    missRecordSize = (uint32_t)math::calcAligned2(missRecordSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    hitGroupRecordSize = (uint32_t)math::calcAligned2(hitGroupRecordSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    callableRecordSize = (uint32_t)math::calcAligned2(callableRecordSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

    // Store strides for use during dispatch
    m_rayGenRecordStride = raygenRecordSize;
    m_missRecordStride = missRecordSize;
    m_hitGroupRecordStride = hitGroupRecordSize;
    m_callableRecordStride = callableRecordSize;

    uint32_t raygenTableSize = m_rayGenShaderCount * raygenRecordSize;
    uint32_t missTableSize = m_missShaderCount * missRecordSize;
    uint32_t hitgroupTableSize = m_hitGroupCount * hitGroupRecordSize;
    uint32_t callableTableSize = m_callableShaderCount * callableRecordSize;
    m_rayGenTableOffset = 0;
    m_missTableOffset = raygenTableSize;
    m_hitGroupTableOffset =
        (uint32_t)math::calcAligned2(m_missTableOffset + missTableSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    m_callableTableOffset = (uint32_t)
        math::calcAligned2(m_hitGroupTableOffset + hitgroupTableSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    uint32_t tableSize = m_callableTableOffset + callableTableSize;

    ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
    pipeline->m_stateObject->QueryInterface(stateObjectProperties.writeRef());

    auto writeTableEntry = [&](void* dest, const std::string& name, const ShaderRecordOverwrite* overwrite)
    {
        if (!name.empty())
        {
            void* shaderId = stateObjectProperties->GetShaderIdentifier(string::to_wstring(name).data());
            memcpy(dest, shaderId, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        }
        if (overwrite && overwrite->size > 0)
        {
            memcpy((uint8_t*)dest + overwrite->offset, overwrite->data, overwrite->size);
        }
    };

    auto tableData = std::make_unique<uint8_t[]>(tableSize);
    uint8_t* tablePtr = tableData.get();
    memset(tablePtr, 0, tableSize);

    for (uint32_t i = 0; i < m_rayGenShaderCount; i++)
    {
        writeTableEntry(
            tablePtr + m_rayGenTableOffset + i * raygenRecordSize,
            m_rayGenShaderEntryPointNames[i],
            i < m_rayGenRecordOverwrites.size() ? &m_rayGenRecordOverwrites[i] : nullptr
        );
    }

    for (uint32_t i = 0; i < m_missShaderCount; i++)
    {
        writeTableEntry(
            tablePtr + m_missTableOffset + i * missRecordSize,
            m_missShaderEntryPointNames[i],
            i < m_missRecordOverwrites.size() ? &m_missRecordOverwrites[i] : nullptr
        );
    }

    for (uint32_t i = 0; i < m_hitGroupCount; i++)
    {
        writeTableEntry(
            tablePtr + m_hitGroupTableOffset + i * hitGroupRecordSize,
            m_hitGroupNames[i],
            i < m_hitGroupRecordOverwrites.size() ? &m_hitGroupRecordOverwrites[i] : nullptr
        );
    }

    for (uint32_t i = 0; i < m_callableShaderCount; i++)
    {
        writeTableEntry(
            tablePtr + m_callableTableOffset + i * callableRecordSize,
            m_callableShaderEntryPointNames[i],
            i < m_callableRecordOverwrites.size() ? &m_callableRecordOverwrites[i] : nullptr
        );
    }

    ComPtr<IBuffer> buffer;
    BufferDesc bufferDesc = {};
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    bufferDesc.defaultState = ResourceState::ShaderResource;
    bufferDesc.usage = BufferUsage::ShaderTable;
    bufferDesc.size = tableSize;
    m_device->createBuffer(bufferDesc, tableData.get(), buffer.writeRef());

    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer.get());
    m_buffers[pipeline] = bufferImpl;
    return bufferImpl;
}

} // namespace rhi::d3d12
