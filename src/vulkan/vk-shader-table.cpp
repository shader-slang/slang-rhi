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

/// Find the entry point index in the root object layout by name.
static uint32_t findEntryPointIndexByName(
    RootShaderObjectLayoutImpl* layout,
    slang::ProgramLayout* programLayout,
    const std::string& name
)
{
    SlangUInt count = programLayout->getEntryPointCount();
    for (SlangUInt i = 0; i < count; ++i)
    {
        auto ep = programLayout->getEntryPointByIndex(i);
        if (ep->getName() == name)
            return i;
    }
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
    slang::ProgramLayout* programLayout = rootLayout->getSlangProgramLayout();

    // Build raygen infos and calculate per-raygen record sizes based on entry point params.
    // Each raygen shader gets its own record size based on its actual parameter requirements.
    uint32_t raygenTableOffset = 0;
    short_vector<RaygenInfo> raygenInfos;

    for (uint32_t i = 0; i < m_rayGenShaderCount; i++)
    {
        // Get the entry point index and parameter size for this raygen shader, if it exists in the root layout.
        const std::string& entryPointName = m_rayGenShaderEntryPointNames[i];
        uint32_t entryPointIndex = findEntryPointIndexByName(rootLayout, programLayout, entryPointName);
        size_t paramsSize = entryPointIndex == uint32_t(-1) ? 0 : rootLayout->getEntryPoint(entryPointIndex).paramsSize;

        // Record size = handle + params, considering any shader record overwrite
        uint32_t recordSize = handleSize + (uint32_t)paramsSize;
        if (i < m_rayGenRecordOverwrites.size())
        {
            uint32_t overwriteEnd = m_rayGenRecordOverwrites[i].offset + m_rayGenRecordOverwrites[i].size;
            recordSize = max(recordSize, overwriteEnd);
        }
        recordSize = max(recordSize, handleSize); // At minimum, we need space for the handle

        // Align record size to shaderGroupBaseAlignment
        recordSize = (uint32_t)math::calcAligned2(recordSize, rtpProps.shaderGroupBaseAlignment);

        RaygenInfo info;
        info.entryPointIndex = entryPointIndex;
        info.paramsSize = paramsSize;
        info.recordOffset = raygenTableOffset;
        info.recordSize = recordSize;
        info.sbtOffset = raygenTableOffset + handleSize;
        raygenInfos.push_back(info);

        raygenTableOffset += recordSize;
    }

    // Calculate record sizes (without alignment).
    uint32_t missRecordSize = max(handleSize, m_missRecordOverwriteMaxSize);
    uint32_t hitGroupRecordSize = max(handleSize, m_hitGroupRecordOverwriteMaxSize);
    uint32_t callableRecordSize = max(handleSize, m_callableRecordOverwriteMaxSize);

    // Align all record sizes to shaderGroupBaseAlignment.
    missRecordSize = (uint32_t)math::calcAligned2(missRecordSize, rtpProps.shaderGroupBaseAlignment);
    hitGroupRecordSize = (uint32_t)math::calcAligned2(hitGroupRecordSize, rtpProps.shaderGroupBaseAlignment);
    callableRecordSize = (uint32_t)math::calcAligned2(callableRecordSize, rtpProps.shaderGroupBaseAlignment);

    // Calculate table sizes.
    uint32_t raygenTableSize = raygenTableOffset;
    uint32_t missTableSize = m_missShaderCount * missRecordSize;
    uint32_t hitTableSize = m_hitGroupCount * hitGroupRecordSize;
    uint32_t callableTableSize = m_callableShaderCount * callableRecordSize;

    uint32_t tableSize = raygenTableSize + missTableSize + hitTableSize + callableTableSize;

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
            tablePtr + raygenInfos[i].recordOffset,
            m_rayGenShaderEntryPointNames[i],
            i < m_rayGenRecordOverwrites.size() ? &m_rayGenRecordOverwrites[i] : nullptr
        );
    }
    tablePtr += raygenTableSize;

    for (uint32_t i = 0; i < m_missShaderCount; i++)
    {
        writeTableEntry(
            tablePtr + i * missRecordSize,
            m_missShaderEntryPointNames[i],
            i < m_missRecordOverwrites.size() ? &m_missRecordOverwrites[i] : nullptr
        );
    }
    tablePtr += missTableSize;

    for (uint32_t i = 0; i < m_hitGroupCount; i++)
    {
        writeTableEntry(
            tablePtr + i * hitGroupRecordSize,
            m_hitGroupNames[i],
            i < m_hitGroupRecordOverwrites.size() ? &m_hitGroupRecordOverwrites[i] : nullptr
        );
    }
    tablePtr += hitTableSize;

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
    if (SLANG_FAILED(device->createBuffer(bufferDesc, tableData.get(), buffer.writeRef())))
    {
        SLANG_RHI_ASSERT_FAILURE("Failed to create shader table buffer");
        return nullptr;
    }

    RefPtr<PipelineData> pipelineData = new PipelineData();

    pipelineData->buffer = checked_cast<BufferImpl*>(buffer.get());
    pipelineData->raygenInfos = std::move(raygenInfos);

    pipelineData->missRecordStride = missRecordSize;
    pipelineData->hitGroupRecordStride = hitGroupRecordSize;
    pipelineData->callableRecordStride = callableRecordSize;

    pipelineData->raygenTableSize = raygenTableSize;
    pipelineData->missTableSize = missTableSize;
    pipelineData->hitTableSize = hitTableSize;
    pipelineData->callableTableSize = callableTableSize;

    m_pipelineData.emplace(pipeline, pipelineData);
    return pipelineData.get();
}

} // namespace rhi::vk
