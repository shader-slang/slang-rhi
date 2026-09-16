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

    auto getRecordSize = [&](const char* sectionName,
                             const std::vector<OwnedRecord>& records,
                             const std::vector<std::string>& names,
                             Size* outSize)
    {
        SLANG_RHI_ASSERT(records.size() == names.size());
        Size size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        for (size_t i = 0; i < records.size(); ++i)
        {
            const auto& record = records[i];
            const auto* structuralRecordInfo = pipeline->findStructuralRecordInfo(names[i]);
            Size nativeRecordSize = 0;
            if (structuralRecordInfo)
            {
                Size localRootArgumentSize = 0;
                if (!tryMultiplySize(
                        structuralRecordInfo->localRootCBVCount,
                        sizeof(D3D12_GPU_VIRTUAL_ADDRESS),
                        &localRootArgumentSize
                    ) ||
                    !tryAddSize(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, localRootArgumentSize, &nativeRecordSize))
                {
                    SLANG_RHI_ASSERT_FAILURE("D3D12 local shader-record size overflowed.");
                    return false;
                }

                if (structuralRecordInfo->hasStructuralRecord)
                {
                    if (record.data.size() > structuralRecordInfo->dataSize)
                    {
                        m_device->printError(
                            "D3D12 structural %s shader record %zu provides %zu application-data bytes, "
                            "but reflection requires at most %zu bytes.\n",
                            sectionName,
                            i,
                            record.data.size(),
                            structuralRecordInfo->dataSize
                        );
                        return false;
                    }
                    if (record.overwrite.size > 0)
                    {
                        m_device->printError(
                            "D3D12 structural %s shader record %zu cannot combine semantic Record "
                            "data with a legacy native-record overwrite.\n",
                            sectionName,
                            i
                        );
                        return false;
                    }
                }
                else
                {
                    // A hit group can inherit this local root signature solely because it shares a
                    // stage with another group. None of its own stages declares the structural
                    // Record bindings represented by these CBVs, so retain the legacy raw-data ABI
                    // while still reserving the component signature's full local-root footprint in
                    // the native record stride.
                    nativeRecordSize =
                        std::max(nativeRecordSize, record.getSize(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES));
                }
            }
            else
            {
                nativeRecordSize = record.getSize(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
            }
            size = std::max(size, nativeRecordSize);
        }
        *outSize = size;
        return true;
    };

    // Calculate record sizes (without alignment).
    Size raygenRecordSize = max(uint32_t(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES), m_rayGenRecordOverwriteMaxSize);
    Size missRecordSize = 0;
    Size hitGroupRecordSize = 0;
    Size callableRecordSize = 0;
    if (!getRecordSize("miss", m_missRecords, m_missShaderEntryPointNames, &missRecordSize) ||
        !getRecordSize("hit-group", m_hitGroupRecords, m_hitGroupNames, &hitGroupRecordSize) ||
        !getRecordSize("callable", m_callableRecords, m_callableShaderEntryPointNames, &callableRecordSize))
    {
        return nullptr;
    }

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
    if (raygenRecordSize > D3D12_RAYTRACING_MAX_SHADER_RECORD_STRIDE ||
        missRecordSize > D3D12_RAYTRACING_MAX_SHADER_RECORD_STRIDE ||
        hitGroupRecordSize > D3D12_RAYTRACING_MAX_SHADER_RECORD_STRIDE ||
        callableRecordSize > D3D12_RAYTRACING_MAX_SHADER_RECORD_STRIDE)
    {
        m_device->printError(
            "A D3D12 native shader-record stride exceeds the %u-byte API limit.\n",
            D3D12_RAYTRACING_MAX_SHADER_RECORD_STRIDE
        );
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

    // Structural application data lives in a separate constant-buffer allocation so arbitrary
    // ConstantBuffer<Record> layouts do not consume the finite local-root-signature budget. Keep
    // every address 256-byte aligned so it is valid as a root CBV.
    Size structuralRecordBufferSize = 0;
    auto allocateStructuralRecords =
        [&](const std::vector<OwnedRecord>& records, const std::vector<std::string>& names, std::vector<Size>& offsets)
    {
        SLANG_RHI_ASSERT(records.size() == names.size());
        offsets.resize(records.size());
        for (size_t i = 0; i < records.size(); ++i)
        {
            const auto* structuralRecordInfo = pipeline->findStructuralRecordInfo(names[i]);
            if (!structuralRecordInfo || !structuralRecordInfo->hasStructuralRecord)
                continue;
            if (!tryAlignSize(
                    structuralRecordBufferSize,
                    D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
                    &structuralRecordBufferSize
                ))
            {
                return false;
            }
            offsets[i] = structuralRecordBufferSize;
            // A non-void Record can still have a zero-byte target layout. Reserve one byte so the
            // local root argument receives a valid aligned CBV address in that case.
            Size allocationSize = std::max(structuralRecordInfo->dataSize, Size(1));
            if (!tryAddSize(structuralRecordBufferSize, allocationSize, &structuralRecordBufferSize))
                return false;
        }
        return true;
    };

    std::vector<Size> missStructuralRecordOffsets;
    std::vector<Size> hitGroupStructuralRecordOffsets;
    std::vector<Size> callableStructuralRecordOffsets;
    if (!allocateStructuralRecords(m_missRecords, m_missShaderEntryPointNames, missStructuralRecordOffsets) ||
        !allocateStructuralRecords(m_hitGroupRecords, m_hitGroupNames, hitGroupStructuralRecordOffsets) ||
        !allocateStructuralRecords(
            m_callableRecords,
            m_callableShaderEntryPointNames,
            callableStructuralRecordOffsets
        ) ||
        !tryAlignSize(
            structuralRecordBufferSize,
            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
            &structuralRecordBufferSize
        ))
    {
        m_device->printError("D3D12 structural shader-record backing storage size overflowed.\n");
        return nullptr;
    }

    ComPtr<IBuffer> structuralRecordBuffer;
    if (structuralRecordBufferSize > 0)
    {
        std::vector<uint8_t> structuralRecordData(structuralRecordBufferSize, 0);
        auto copyStructuralRecords = [&](const std::vector<OwnedRecord>& records,
                                         const std::vector<std::string>& names,
                                         const std::vector<Size>& offsets)
        {
            SLANG_RHI_ASSERT(records.size() == names.size());
            SLANG_RHI_ASSERT(records.size() == offsets.size());
            for (size_t i = 0; i < records.size(); ++i)
            {
                const auto* structuralRecordInfo = pipeline->findStructuralRecordInfo(names[i]);
                if (!structuralRecordInfo || !structuralRecordInfo->hasStructuralRecord || records[i].data.empty())
                    continue;
                memcpy(structuralRecordData.data() + offsets[i], records[i].data.data(), records[i].data.size());
            }
        };
        copyStructuralRecords(m_missRecords, m_missShaderEntryPointNames, missStructuralRecordOffsets);
        copyStructuralRecords(m_hitGroupRecords, m_hitGroupNames, hitGroupStructuralRecordOffsets);
        copyStructuralRecords(m_callableRecords, m_callableShaderEntryPointNames, callableStructuralRecordOffsets);

        BufferDesc recordBufferDesc = {};
        recordBufferDesc.memoryType = MemoryType::DeviceLocal;
        recordBufferDesc.defaultState = ResourceState::ConstantBuffer;
        recordBufferDesc.usage = BufferUsage::ConstantBuffer;
        recordBufferDesc.size = structuralRecordBufferSize;
        if (SLANG_FAILED(
                m_device->createBuffer(recordBufferDesc, structuralRecordData.data(), structuralRecordBuffer.writeRef())
            ))
        {
            SLANG_RHI_ASSERT_FAILURE("Failed to create D3D12 structural shader-record backing buffer");
            return nullptr;
        }
        SLANG_RHI_ASSERT(
            structuralRecordBuffer->getDeviceAddress() % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT == 0
        );
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

    auto writeOwnedTableEntry =
        [&](void* dest, const std::string& name, const OwnedRecord& record, Size structuralRecordOffset)
    {
        writeTableEntry(dest, name, nullptr);
        if (const auto* structuralRecordInfo = pipeline->findStructuralRecordInfo(name))
        {
            if (structuralRecordInfo->hasStructuralRecord)
            {
                SLANG_RHI_ASSERT(structuralRecordBuffer);
                D3D12_GPU_VIRTUAL_ADDRESS address = structuralRecordBuffer->getDeviceAddress() + structuralRecordOffset;

                // A composed hit group can reference stages whose compiler-reserved cbuffer
                // bindings differ. Its local root signature therefore contains one CBV parameter
                // per unique binding. They all expose the same logical Record, so repeat the same
                // backing address for every parameter in the exact order used to create the root
                // signature.
                uint8_t* localRootArguments = static_cast<uint8_t*>(dest) + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
                for (uint32_t i = 0; i < structuralRecordInfo->localRootCBVCount; ++i)
                {
                    memcpy(localRootArguments + Size(i) * sizeof(address), &address, sizeof(address));
                }
            }
            else
            {
                // This group only inherits the component local root signature. Its own stages do
                // not declare the inherited structural Record bindings, so its application bytes
                // keep the legacy inline layout and legacy overwrites retain their existing
                // precedence.
                record.writeData(dest, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
            }
        }
        else
        {
            record.writeData(dest, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        }
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
            m_missRecords[i],
            missStructuralRecordOffsets[i]
        );
    }

    for (uint32_t i = 0; i < m_hitGroupCount; i++)
    {
        writeOwnedTableEntry(
            tablePtr + hitGroupTableOffset + Size(i) * hitGroupRecordSize,
            m_hitGroupNames[i],
            m_hitGroupRecords[i],
            hitGroupStructuralRecordOffsets[i]
        );
    }

    for (uint32_t i = 0; i < m_callableShaderCount; i++)
    {
        writeOwnedTableEntry(
            tablePtr + callableTableOffset + Size(i) * callableRecordSize,
            m_callableShaderEntryPointNames[i],
            m_callableRecords[i],
            callableStructuralRecordOffsets[i]
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
    if (structuralRecordBuffer)
        pipelineData->structuralRecordBuffer = checked_cast<BufferImpl*>(structuralRecordBuffer.get());

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
