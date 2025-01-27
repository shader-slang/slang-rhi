#include "cuda-shader-table.h"
#include "cuda-device.h"
#include "cuda-pipeline.h"

#include <vector>

#if SLANG_RHI_ENABLE_OPTIX

namespace rhi::cuda {

struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) SbtRecord
{
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
};

ShaderTableImpl::~ShaderTableImpl()
{
    for (auto it : m_instances)
    {
        SLANG_CUDA_ASSERT_ON_FAIL(cuMemFree(it.second.buffer));
    }
}

ShaderTableImpl::Instance* ShaderTableImpl::getInstance(RayTracingPipelineImpl* pipeline)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto instanceIt = m_instances.find(pipeline);
    if (instanceIt != m_instances.end())
        return &instanceIt->second;

    Instance& instance = m_instances[pipeline];
    std::memset(&instance, 0, sizeof(instance));

    instance.raygenRecordSize = sizeof(SbtRecord);

    size_t tableSize =
        (m_rayGenShaderCount + m_missShaderCount + m_hitGroupCount + m_callableShaderCount) * sizeof(SbtRecord);

    auto hostBuffer = std::make_unique<uint8_t[]>(tableSize);
    std::memset(hostBuffer.get(), 0, tableSize);
    auto hostPtr = hostBuffer.get();

    SLANG_CUDA_ASSERT_ON_FAIL(cuMemAlloc(&instance.buffer, tableSize));
    CUdeviceptr deviceBuffer = instance.buffer;
    CUdeviceptr devicePtr = deviceBuffer;

    OptixShaderBindingTable& sbt = instance.sbt;

    size_t shaderTableEntryIndex = 0;

    if (m_rayGenShaderCount > 0)
    {
        sbt.raygenRecord = devicePtr;
        for (uint32_t i = 0; i < m_rayGenShaderCount; i++)
        {
            auto it = pipeline->m_shaderGroupNameToIndex.find(m_shaderGroupNames[shaderTableEntryIndex++]);
            if (it == pipeline->m_shaderGroupNameToIndex.end())
                continue;
            SLANG_OPTIX_ASSERT_ON_FAIL(optixSbtRecordPackHeader(pipeline->m_programGroups[it->second], hostPtr));
            hostPtr += sizeof(SbtRecord);
            devicePtr += sizeof(SbtRecord);
        }
    }

    if (m_missShaderCount > 0)
    {
        sbt.missRecordBase = devicePtr;
        sbt.missRecordStrideInBytes = sizeof(SbtRecord);
        sbt.missRecordCount = m_missShaderCount;
        for (uint32_t i = 0; i < m_missShaderCount; i++)
        {
            auto it = pipeline->m_shaderGroupNameToIndex.find(m_shaderGroupNames[shaderTableEntryIndex++]);
            if (it == pipeline->m_shaderGroupNameToIndex.end())
                continue;
            SLANG_OPTIX_ASSERT_ON_FAIL(optixSbtRecordPackHeader(pipeline->m_programGroups[it->second], hostPtr));
            hostPtr += sizeof(SbtRecord);
            devicePtr += sizeof(SbtRecord);
        }
    }

    if (m_hitGroupCount > 0)
    {
        sbt.hitgroupRecordBase = devicePtr;
        sbt.hitgroupRecordStrideInBytes = sizeof(SbtRecord);
        sbt.hitgroupRecordCount = m_hitGroupCount;
        for (uint32_t i = 0; i < m_hitGroupCount; i++)
        {
            auto it = pipeline->m_shaderGroupNameToIndex.find(m_shaderGroupNames[shaderTableEntryIndex++]);
            if (it == pipeline->m_shaderGroupNameToIndex.end())
                continue;
            SLANG_OPTIX_ASSERT_ON_FAIL(optixSbtRecordPackHeader(pipeline->m_programGroups[it->second], hostPtr));
            hostPtr += sizeof(SbtRecord);
            devicePtr += sizeof(SbtRecord);
        }
    }

    if (m_callableShaderCount > 0)
    {
        sbt.callablesRecordBase = devicePtr;
        sbt.callablesRecordStrideInBytes = sizeof(SbtRecord);
        sbt.callablesRecordCount = m_callableShaderCount;
        for (uint32_t i = 0; i < m_callableShaderCount; i++)
        {
            auto it = pipeline->m_shaderGroupNameToIndex.find(m_shaderGroupNames[shaderTableEntryIndex++]);
            if (it == pipeline->m_shaderGroupNameToIndex.end())
                continue;
            SLANG_OPTIX_ASSERT_ON_FAIL(optixSbtRecordPackHeader(pipeline->m_programGroups[it->second], hostPtr));
            hostPtr += sizeof(SbtRecord);
            devicePtr += sizeof(SbtRecord);
        }
    }

    SLANG_CUDA_ASSERT_ON_FAIL(cuMemcpyHtoD(deviceBuffer, hostBuffer.get(), tableSize));

    return &instance;
}

} // namespace rhi::cuda

#endif // SLANG_RHI_ENABLE_OPTIX
