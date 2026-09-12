#include "vk-shader-table.h"
#include "vk-device.h"
#include "vk-buffer.h"
#include "vk-pipeline.h"
#include "vk-command.h"
#include "vk-shader-object-layout.h"

#include <vector>

namespace rhi::vk {

ShaderTableImpl::ShaderTableImpl(Device* device, const ShaderTableDesc& desc)
    : ShaderTable(device, desc)
{
}

/// Find the entry point index using the cached map on the pipeline.
static uint32_t findEntryPointIndexByName(RayTracingPipelineImpl* pipeline, const std::string& name)
{
    auto it = pipeline->m_entryPointIndexByName.find(name);
    if (it != pipeline->m_entryPointIndexByName.end())
        return it->second;
    return uint32_t(-1);
}

ShaderTableImpl::PipelineData* ShaderTableImpl::getPipelineData(RayTracingPipelineImpl* pipeline)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    DeviceImpl* device = getDevice<DeviceImpl>();

    auto it = m_pipelineData.find(pipeline);
    if (it != m_pipelineData.end())
        return it->second.get();

    auto& api = device->m_api;
    const auto& rtpProps = api.m_rayTracingPipelineProperties;
    uint32_t handleSize = rtpProps.shaderGroupHandleSize;

    RootShaderObjectLayoutImpl* rootLayout = pipeline->m_rootObjectLayout;

    // Build raygen infos and calculate per-raygen record sizes based on entry point params.
    // Each raygen shader gets its own record size based on its actual parameter requirements.
    Size raygenTableOffset = 0;
    short_vector<RaygenInfo> raygenInfos;

    for (uint32_t i = 0; i < m_rayGenShaderCount; i++)
    {
        // Get the entry point index and parameter size for this raygen shader, if it exists in the root layout.
        const std::string& entryPointName = m_rayGenShaderEntryPointNames[i];
        uint32_t entryPointIndex = findEntryPointIndexByName(pipeline, entryPointName);
        size_t paramsSize = entryPointIndex == uint32_t(-1) ? 0 : rootLayout->getEntryPoint(entryPointIndex).paramsSize;

        // Record size = handle + params, considering any shader record overwrite
        Size recordSize = 0;
        if (!tryAddSize(handleSize, paramsSize, &recordSize))
        {
            SLANG_RHI_ASSERT_FAILURE("Ray-generation shader record size overflowed.");
            return nullptr;
        }
        if (i < m_rayGenRecordOverwrites.size())
        {
            Size overwriteEnd = Size(m_rayGenRecordOverwrites[i].offset) + Size(m_rayGenRecordOverwrites[i].size);
            recordSize = max(recordSize, overwriteEnd);
        }
        recordSize = max(recordSize, Size(handleSize)); // At minimum, we need space for the handle

        // Align record size to shaderGroupBaseAlignment
        if (!tryAlignSize(recordSize, rtpProps.shaderGroupBaseAlignment, &recordSize))
        {
            SLANG_RHI_ASSERT_FAILURE("Ray-generation shader record alignment overflowed.");
            return nullptr;
        }

        RaygenInfo info;
        info.entryPointIndex = entryPointIndex;
        info.paramsSize = paramsSize;
        info.recordOffset = raygenTableOffset;
        info.recordSize = recordSize;
        info.sbtOffset = raygenTableOffset + handleSize;
        raygenInfos.push_back(info);

        if (!tryAddSize(raygenTableOffset, recordSize, &raygenTableOffset))
        {
            SLANG_RHI_ASSERT_FAILURE("Ray-generation shader table size overflowed.");
            return nullptr;
        }
    }

    auto getAlignedRecordSize = [&](const std::vector<OwnedRecord>& records, Size* outSize)
    {
        if (!tryAlignSize(getMaxRecordSize(records, handleSize), rtpProps.shaderGroupBaseAlignment, outSize))
            return false;
        SLANG_RHI_ASSERT(*outSize <= UINT32_MAX);
        return true;
    };

    Size missRecordSize = 0;
    Size hitGroupRecordSize = 0;
    Size callableRecordSize = 0;
    if (!getAlignedRecordSize(m_missRecords, &missRecordSize) ||
        !getAlignedRecordSize(m_hitGroupRecords, &hitGroupRecordSize) ||
        !getAlignedRecordSize(m_callableRecords, &callableRecordSize))
    {
        SLANG_RHI_ASSERT_FAILURE("Shader table record-size alignment overflowed.");
        return nullptr;
    }

    // Compute every section in `Size` before allocating the table. A single large record determines
    // the stride of its whole section, so multiplying in uint32_t could otherwise wrap even when
    // most records contain no application data.
    Size raygenTableSize = raygenTableOffset;
    Size missTableSize = 0;
    Size hitTableSize = 0;
    Size callableTableSize = 0;
    Size tableSize = 0;
    if (!tryMultiplySize(m_missShaderCount, missRecordSize, &missTableSize) ||
        !tryMultiplySize(m_hitGroupCount, hitGroupRecordSize, &hitTableSize) ||
        !tryMultiplySize(m_callableShaderCount, callableRecordSize, &callableTableSize) ||
        !tryAddSize(raygenTableSize, missTableSize, &tableSize) || !tryAddSize(tableSize, hitTableSize, &tableSize) ||
        !tryAddSize(tableSize, callableTableSize, &tableSize))
    {
        SLANG_RHI_ASSERT_FAILURE("Shader table layout size overflowed.");
        return nullptr;
    }

    std::vector<uint8_t> handles;
    auto handleCount = pipeline->m_shaderGroupCount;
    Size totalHandleSize = 0;
    if (!tryMultiplySize(handleSize, handleCount, &totalHandleSize))
    {
        SLANG_RHI_ASSERT_FAILURE("Shader-group handle table size overflowed.");
        return nullptr;
    }
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
            auto src = handles.data() + Size(it->second) * handleSize;
            memcpy(dest, src, handleSize);
        }
        if (overwrite && overwrite->size > 0)
        {
            memcpy((uint8_t*)dest + overwrite->offset, overwrite->data, overwrite->size);
        }
    };

    auto writeOwnedTableEntry = [&](void* dest, const std::string& name, const OwnedRecord& record)
    {
        writeTableEntry(dest, name, nullptr);
        record.writeData(dest, handleSize);
    };

    auto tableData = std::make_unique<uint8_t[]>(tableSize);
    uint8_t* tablePtr = tableData.get();
    memset(tableData.get(), 0, tableSize);

    for (uint32_t i = 0; i < m_rayGenShaderCount; i++)
    {
        writeTableEntry(
            tablePtr + raygenInfos[i].recordOffset,
            m_rayGenShaderEntryPointNames[i],
            i < m_rayGenRecordOverwrites.size() ? &m_rayGenRecordOverwrites[i] : nullptr
        );
    }
    tablePtr += raygenTableSize;

    for (uint32_t i = 0; i < m_missShaderCount; i++)
    {
        writeOwnedTableEntry(tablePtr + Size(i) * missRecordSize, m_missShaderEntryPointNames[i], m_missRecords[i]);
    }
    tablePtr += missTableSize;

    for (uint32_t i = 0; i < m_hitGroupCount; i++)
    {
        writeOwnedTableEntry(tablePtr + Size(i) * hitGroupRecordSize, m_hitGroupNames[i], m_hitGroupRecords[i]);
    }
    tablePtr += hitTableSize;

    for (uint32_t i = 0; i < m_callableShaderCount; i++)
    {
        writeOwnedTableEntry(
            tablePtr + Size(i) * callableRecordSize,
            m_callableShaderEntryPointNames[i],
            m_callableRecords[i]
        );
    }

    ComPtr<IBuffer> buffer;
    BufferDesc bufferDesc = {};
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    bufferDesc.usage = BufferUsage::ShaderTable | BufferUsage::CopyDestination;
    bufferDesc.defaultState = ResourceState::General;

    // Vulkan does not guarantee that the buffer's base device address satisfies
    // shaderGroupBaseAlignment, so reserve enough space to align the SBT within it.
    const Size tableAlignment = max<Size>(1, rtpProps.shaderGroupBaseAlignment);
    Size bufferSize = 0;
    if (!tryAddSize(tableSize, tableAlignment - 1, &bufferSize))
    {
        SLANG_RHI_ASSERT_FAILURE("Aligned shader table buffer size overflowed.");
        return nullptr;
    }
    bufferDesc.size = bufferSize;
    if (SLANG_FAILED(device->createBuffer(bufferDesc, nullptr, buffer.writeRef())))
    {
        SLANG_RHI_ASSERT_FAILURE("Failed to create shader table buffer");
        return nullptr;
    }

    const DeviceAddress bufferAddress = buffer->getDeviceAddress();
    const DeviceAddress alignedTableAddress =
        bufferAddress + (tableAlignment - bufferAddress % tableAlignment) % tableAlignment;
    const uint32_t tableOffset = (uint32_t)(alignedTableAddress - bufferAddress);
    SLANG_RHI_ASSERT((bufferAddress + tableOffset) % tableAlignment == 0);

    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer.get());
    if (SLANG_FAILED(device->uploadBufferInitData(bufferImpl, tableOffset, tableSize, tableData.get())))
    {
        SLANG_RHI_ASSERT_FAILURE("Failed to upload shader table data");
        return nullptr;
    }

    RefPtr<PipelineData> pipelineData = new PipelineData();

    pipelineData->buffer = bufferImpl;
    pipelineData->raygenInfos = std::move(raygenInfos);
    pipelineData->tableOffset = tableOffset;

    pipelineData->missRecordStride = uint32_t(missRecordSize);
    pipelineData->hitGroupRecordStride = uint32_t(hitGroupRecordSize);
    pipelineData->callableRecordStride = uint32_t(callableRecordSize);

    pipelineData->raygenTableSize = raygenTableSize;
    pipelineData->missTableSize = missTableSize;
    pipelineData->hitTableSize = hitTableSize;
    pipelineData->callableTableSize = callableTableSize;

    m_pipelineData.emplace(pipeline, pipelineData);
    return pipelineData.get();
}

} // namespace rhi::vk
