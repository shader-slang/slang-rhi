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

    // Calculate record sizes by checking overwrites (similar to Vulkan implementation)
    uint32_t raygenRecordSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    uint32_t missRecordSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    uint32_t hitGroupRecordSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    uint32_t callableRecordSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

    // The base class stores overwrites in order: raygen, miss, hitgroup, callable
    // Calculate base indices for each section
    uint32_t raygenOverwriteBase = 0;
    uint32_t missOverwriteBase = m_rayGenShaderCount;
    uint32_t hitGroupOverwriteBase = m_rayGenShaderCount + m_missShaderCount;
    uint32_t callableOverwriteBase = m_rayGenShaderCount + m_missShaderCount + m_hitGroupCount;

    // Check raygen records
    for (uint32_t i = 0; i < m_rayGenShaderCount; i++)
    {
        const ShaderRecordOverwrite& overwrite = m_recordOverwrites[raygenOverwriteBase + i];
        if (overwrite.size > 0)
        {
            uint32_t requiredSize = overwrite.offset + overwrite.size;
            raygenRecordSize = std::max(raygenRecordSize, requiredSize);
        }
    }

    // Check miss records
    for (uint32_t i = 0; i < m_missShaderCount; i++)
    {
        const ShaderRecordOverwrite& overwrite = m_recordOverwrites[missOverwriteBase + i];
        if (overwrite.size > 0)
        {
            uint32_t requiredSize = overwrite.offset + overwrite.size;
            missRecordSize = std::max(missRecordSize, requiredSize);
        }
    }

    // Check hit group records
    for (uint32_t i = 0; i < m_hitGroupCount; i++)
    {
        const ShaderRecordOverwrite& overwrite = m_recordOverwrites[hitGroupOverwriteBase + i];
        if (overwrite.size > 0)
        {
            uint32_t requiredSize = overwrite.offset + overwrite.size;
            hitGroupRecordSize = std::max(hitGroupRecordSize, requiredSize);
        }
    }

    // Check callable records
    for (uint32_t i = 0; i < m_callableShaderCount; i++)
    {
        const ShaderRecordOverwrite& overwrite = m_recordOverwrites[callableOverwriteBase + i];
        if (overwrite.size > 0)
        {
            uint32_t requiredSize = overwrite.offset + overwrite.size;
            callableRecordSize = std::max(callableRecordSize, requiredSize);
        }
    }

    // Align all record sizes to D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT
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
            tablePtr + m_rayGenTableOffset + raygenRecordSize * i,
            m_shaderGroupNames[i],
            m_recordOverwrites[i]
        );
    }
    for (uint32_t i = 0; i < m_missShaderCount; i++)
    {
        copyShaderIdInto(
            tablePtr + m_missTableOffset + missRecordSize * i,
            m_shaderGroupNames[m_rayGenShaderCount + i],
            m_recordOverwrites[m_rayGenShaderCount + i]
        );
    }
    for (uint32_t i = 0; i < m_hitGroupCount; i++)
    {
        copyShaderIdInto(
            tablePtr + m_hitGroupTableOffset + hitGroupRecordSize * i,
            m_shaderGroupNames[m_rayGenShaderCount + m_missShaderCount + i],
            m_recordOverwrites[m_rayGenShaderCount + m_missShaderCount + i]
        );
    }
    for (uint32_t i = 0; i < m_callableShaderCount; i++)
    {
        copyShaderIdInto(
            tablePtr + m_callableTableOffset + callableRecordSize * i,
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
