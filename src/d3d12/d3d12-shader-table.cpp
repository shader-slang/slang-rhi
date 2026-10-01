#include "d3d12-shader-table.h"
#include "d3d12-device.h"
#include "d3d12-buffer.h"
#include "d3d12-pipeline.h"

namespace rhi::d3d12 {

ShaderTableImpl::ShaderTableImpl(Device* device, const ShaderTableDesc& desc)
    : ShaderTable(device, desc)
{
}

ShaderTableImpl::PipelineData* ShaderTableImpl::getPipelineData(RayTracingPipelineImpl* pipeline)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_pipelineData.find(pipeline);
    if (it != m_pipelineData.end())
        return it->second.get();

    auto getRecordSize = [&](const std::vector<OwnedRecord>& records)
    {
        Size size = getMaxRecordSize(records, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        SLANG_RHI_ASSERT(size <= UINT32_MAX);
        return size;
    };

    // Calculate record sizes (without alignment).
    Size raygenRecordSize = max(uint32_t(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES), m_rayGenRecordOverwriteMaxSize);
    Size missRecordSize = getRecordSize(m_missRecords);
    Size hitGroupRecordSize = getRecordSize(m_hitGroupRecords);
    Size callableRecordSize = getRecordSize(m_callableRecords);

    // Align all record sizes to D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, expect raygen records which must be
    // aligned to D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT.
    if (!tryAlignSize(raygenRecordSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, &raygenRecordSize) ||
        !tryAlignSize(missRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, &missRecordSize) ||
        !tryAlignSize(hitGroupRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, &hitGroupRecordSize) ||
        !tryAlignSize(callableRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, &callableRecordSize))
    {
        SLANG_RHI_ASSERT_FAILURE("Shader table record-size alignment overflowed.");
        return nullptr;
    }

    // Compute every section in `Size` before allocating the table. A single large record determines
    // the stride of its whole section, so multiplying in uint32_t could otherwise wrap even when
    // most records contain no application data.
    Size raygenTableSize = 0;
    Size missTableSize = 0;
    Size hitGroupTableSize = 0;
    Size callableTableSize = 0;
    if (!tryMultiplySize(m_rayGenShaderCount, raygenRecordSize, &raygenTableSize) ||
        !tryMultiplySize(m_missShaderCount, missRecordSize, &missTableSize) ||
        !tryMultiplySize(m_hitGroupCount, hitGroupRecordSize, &hitGroupTableSize) ||
        !tryMultiplySize(m_callableShaderCount, callableRecordSize, &callableTableSize))
    {
        SLANG_RHI_ASSERT_FAILURE("Shader table section size overflowed.");
        return nullptr;
    }

    // Calculate table offsets, ensuring each table starts at a multiple of
    // D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT.
    Size rayGenTableOffset = 0;
    Size missTableOffset = 0;
    Size hitGroupTableOffset = 0;
    Size callableTableOffset = 0;
    Size tableSize = 0;
    Size sectionEnd = 0;
    if (!tryAddSize(rayGenTableOffset, raygenTableSize, &sectionEnd) ||
        !tryAlignSize(sectionEnd, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, &missTableOffset) ||
        !tryAddSize(missTableOffset, missTableSize, &sectionEnd) ||
        !tryAlignSize(sectionEnd, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, &hitGroupTableOffset) ||
        !tryAddSize(hitGroupTableOffset, hitGroupTableSize, &sectionEnd) ||
        !tryAlignSize(sectionEnd, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, &callableTableOffset) ||
        !tryAddSize(callableTableOffset, callableTableSize, &tableSize))
    {
        SLANG_RHI_ASSERT_FAILURE("Shader table layout size overflowed.");
        return nullptr;
    }

    auto writeTableEntry = [&](void* dest, const std::string& name, const ShaderRecordOverwrite* overwrite)
    {
        auto it = pipeline->m_shaderIdentifierByName.find(name);
        if (it != pipeline->m_shaderIdentifierByName.end())
        {
            memcpy(dest, it->second, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        }
        if (overwrite && overwrite->size > 0)
        {
            memcpy((uint8_t*)dest + overwrite->offset, overwrite->data, overwrite->size);
        }
    };

    auto writeOwnedTableEntry = [&](void* dest, const std::string& name, const OwnedRecord& record)
    {
        writeTableEntry(dest, name, nullptr);
        record.writeData(dest, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    };

    auto tableData = std::make_unique<uint8_t[]>(tableSize);
    uint8_t* tablePtr = tableData.get();
    memset(tablePtr, 0, tableSize);

    for (uint32_t i = 0; i < m_rayGenShaderCount; i++)
    {
        writeTableEntry(
            tablePtr + rayGenTableOffset + i * raygenRecordSize,
            m_rayGenShaderEntryPointNames[i],
            i < m_rayGenRecordOverwrites.size() ? &m_rayGenRecordOverwrites[i] : nullptr
        );
    }

    for (uint32_t i = 0; i < m_missShaderCount; i++)
    {
        writeOwnedTableEntry(
            tablePtr + missTableOffset + Size(i) * missRecordSize,
            m_missShaderEntryPointNames[i],
            m_missRecords[i]
        );
    }

    for (uint32_t i = 0; i < m_hitGroupCount; i++)
    {
        writeOwnedTableEntry(
            tablePtr + hitGroupTableOffset + Size(i) * hitGroupRecordSize,
            m_hitGroupNames[i],
            m_hitGroupRecords[i]
        );
    }

    for (uint32_t i = 0; i < m_callableShaderCount; i++)
    {
        writeOwnedTableEntry(
            tablePtr + callableTableOffset + Size(i) * callableRecordSize,
            m_callableShaderEntryPointNames[i],
            m_callableRecords[i]
        );
    }

    ComPtr<IBuffer> buffer;
    BufferDesc bufferDesc = {};
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    bufferDesc.defaultState = ResourceState::ShaderResource;
    bufferDesc.usage = BufferUsage::ShaderTable;
    bufferDesc.size = tableSize;
    if (SLANG_FAILED(m_device->createBuffer(bufferDesc, tableData.get(), buffer.writeRef())))
    {
        SLANG_RHI_ASSERT_FAILURE("Failed to create shader table buffer");
        return nullptr;
    }

    RefPtr<PipelineData> pipelineData = new PipelineData();

    pipelineData->buffer = checked_cast<BufferImpl*>(buffer.get());

    // D3D12 should always align allocations to the required minimum.
    SLANG_RHI_ASSERT(buffer->getDeviceAddress() % D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT == 0);

    pipelineData->rayGenTableOffset = rayGenTableOffset;
    pipelineData->missTableOffset = missTableOffset;
    pipelineData->hitGroupTableOffset = hitGroupTableOffset;
    pipelineData->callableTableOffset = callableTableOffset;
    pipelineData->missTableSize = missTableSize;
    pipelineData->hitGroupTableSize = hitGroupTableSize;
    pipelineData->callableTableSize = callableTableSize;
    pipelineData->rayGenRecordStride = uint32_t(raygenRecordSize);
    pipelineData->missRecordStride = uint32_t(missRecordSize);
    pipelineData->hitGroupRecordStride = uint32_t(hitGroupRecordSize);
    pipelineData->callableRecordStride = uint32_t(callableRecordSize);

    m_pipelineData.emplace(pipeline, pipelineData);
    return pipelineData.get();
}

} // namespace rhi::d3d12
