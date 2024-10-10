#include "d3d12-shader-table.h"
#include "d3d12-device.h"
#include "d3d12-pipeline.h"
#include "d3d12-transient-heap.h"

#include "core/string.h"

namespace rhi::d3d12 {

RefPtr<Buffer> ShaderTableImpl::createDeviceBuffer(
    Pipeline* pipeline,
    TransientResourceHeap* transientHeap,
    IRayTracingPassEncoder* encoder
)
{
    uint32_t raygenTableSize = m_rayGenShaderCount * kRayGenRecordSize;
    uint32_t missTableSize = m_missShaderCount * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    uint32_t hitgroupTableSize = m_hitGroupCount * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    uint32_t callableTableSize = m_callableShaderCount * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    m_rayGenTableOffset = 0;
    m_missTableOffset = raygenTableSize;
    m_hitGroupTableOffset =
        (uint32_t)D3DUtil::calcAligned(m_missTableOffset + missTableSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    m_callableTableOffset = (uint32_t
    )D3DUtil::calcAligned(m_hitGroupTableOffset + hitgroupTableSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    uint32_t tableSize = m_callableTableOffset + callableTableSize;

    auto pipelineImpl = checked_cast<RayTracingPipelineImpl*>(pipeline);
    ComPtr<IBuffer> buffer;
    BufferDesc bufferDesc = {};
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    bufferDesc.defaultState = ResourceState::ShaderResource;
    bufferDesc.usage = BufferUsage::ShaderTable;
    bufferDesc.size = tableSize;
    m_device->createBuffer(bufferDesc, nullptr, buffer.writeRef());

    ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
    pipelineImpl->m_stateObject->QueryInterface(stateObjectProperties.writeRef());

    TransientResourceHeapImpl* transientHeapImpl = checked_cast<TransientResourceHeapImpl*>(transientHeap);

    IBuffer* stagingBuffer = nullptr;
    Offset stagingBufferOffset = 0;
    transientHeapImpl->allocateStagingBuffer(tableSize, stagingBuffer, stagingBufferOffset, MemoryType::Upload);

    SLANG_RHI_ASSERT(stagingBuffer);
    void* stagingPtr = nullptr;
    stagingBuffer->map(nullptr, &stagingPtr);

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

    uint8_t* stagingBufferPtr = (uint8_t*)stagingPtr + stagingBufferOffset;
    memset(stagingBufferPtr, 0, tableSize);

    for (uint32_t i = 0; i < m_rayGenShaderCount; i++)
    {
        copyShaderIdInto(
            stagingBufferPtr + m_rayGenTableOffset + kRayGenRecordSize * i,
            m_shaderGroupNames[i],
            m_recordOverwrites[i]
        );
    }
    for (uint32_t i = 0; i < m_missShaderCount; i++)
    {
        copyShaderIdInto(
            stagingBufferPtr + m_missTableOffset + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES * i,
            m_shaderGroupNames[m_rayGenShaderCount + i],
            m_recordOverwrites[m_rayGenShaderCount + i]
        );
    }
    for (uint32_t i = 0; i < m_hitGroupCount; i++)
    {
        copyShaderIdInto(
            stagingBufferPtr + m_hitGroupTableOffset + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES * i,
            m_shaderGroupNames[m_rayGenShaderCount + m_missShaderCount + i],
            m_recordOverwrites[m_rayGenShaderCount + m_missShaderCount + i]
        );
    }
    for (uint32_t i = 0; i < m_callableShaderCount; i++)
    {
        copyShaderIdInto(
            stagingBufferPtr + m_callableTableOffset + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES * i,
            m_shaderGroupNames[m_rayGenShaderCount + m_missShaderCount + m_hitGroupCount + i],
            m_recordOverwrites[m_rayGenShaderCount + m_missShaderCount + m_hitGroupCount + i]
        );
    }

    stagingBuffer->unmap(nullptr);
    checked_cast<RayTracingPassEncoderImpl*>(encoder)->m_commandBuffer->m_cmdList->CopyBufferRegion(
        checked_cast<BufferImpl*>(buffer.get())->m_resource.getResource(),
        0,
        checked_cast<BufferImpl*>(stagingBuffer)->m_resource.getResource(),
        stagingBufferOffset,
        tableSize
    );
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = checked_cast<BufferImpl*>(buffer.get())->m_resource.getResource();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    checked_cast<RayTracingPassEncoderImpl*>(encoder)->m_commandBuffer->m_cmdList->ResourceBarrier(1, &barrier);

    RefPtr<Buffer> resultPtr = checked_cast<Buffer*>(buffer.get());
    return _Move(resultPtr);
}

} // namespace rhi::d3d12
