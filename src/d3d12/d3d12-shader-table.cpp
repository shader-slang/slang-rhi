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

    uint32_t raygenTableSize = m_rayGenShaderCount * kRayGenRecordSize;
    uint32_t missTableSize = m_missShaderCount * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    uint32_t hitgroupTableSize = m_hitGroupCount * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    uint32_t callableTableSize = m_callableShaderCount * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    m_rayGenTableOffset = 0;
    m_missTableOffset = raygenTableSize;
    m_hitGroupTableOffset =
        (uint32_t)math::calcAligned2(m_missTableOffset + missTableSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    m_callableTableOffset = (uint32_t)
        math::calcAligned2(m_hitGroupTableOffset + hitgroupTableSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    uint32_t tableSize = m_callableTableOffset + callableTableSize;

    auto tableData = std::make_unique<uint8_t[]>(tableSize);

    ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
    pipeline->m_stateObject->QueryInterface(stateObjectProperties.writeRef());

    auto copyShaderIdInto = [&](void* dest, std::string& name, const ShaderRecordOverwrite& overwrite)
    {
        if (!name.empty())
        {
            void* shaderId = stateObjectProperties->GetShaderIdentifier(string::to_wstring(name).data());
            memcpy(dest, shaderId, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        }
        if (overwrite.size)
        {
            memcpy((uint8_t*)dest + overwrite.offset, overwrite.data, overwrite.size);
        }
    };

    uint8_t* tablePtr = tableData.get();
    memset(tablePtr, 0, tableSize);

    for (uint32_t i = 0; i < m_rayGenShaderCount; i++)
    {
        copyShaderIdInto(
            tablePtr + m_rayGenTableOffset + kRayGenRecordSize * i,
            m_shaderGroupNames[i],
            m_recordOverwrites[i]
        );
    }
    for (uint32_t i = 0; i < m_missShaderCount; i++)
    {
        copyShaderIdInto(
            tablePtr + m_missTableOffset + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES * i,
            m_shaderGroupNames[m_rayGenShaderCount + i],
            m_recordOverwrites[m_rayGenShaderCount + i]
        );
    }
    for (uint32_t i = 0; i < m_hitGroupCount; i++)
    {
        copyShaderIdInto(
            tablePtr + m_hitGroupTableOffset + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES * i,
            m_shaderGroupNames[m_rayGenShaderCount + m_missShaderCount + i],
            m_recordOverwrites[m_rayGenShaderCount + m_missShaderCount + i]
        );
    }
    for (uint32_t i = 0; i < m_callableShaderCount; i++)
    {
        copyShaderIdInto(
            tablePtr + m_callableTableOffset + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES * i,
            m_shaderGroupNames[m_rayGenShaderCount + m_missShaderCount + m_hitGroupCount + i],
            m_recordOverwrites[m_rayGenShaderCount + m_missShaderCount + m_hitGroupCount + i]
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
