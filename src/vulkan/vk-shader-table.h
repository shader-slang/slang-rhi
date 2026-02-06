#pragma once

#include "vk-base.h"

#include "core/short_vector.h"

namespace rhi::vk {

class ShaderTableImpl : public ShaderTable
{
public:
    /// Information for each raygen shader for copying entry point params to SBT at dispatch time.
    struct RaygenInfo
    {
        uint32_t entryPointIndex; // Index into the root object layout's entry points
        uint64_t sbtOffset;       // Offset within the SBT buffer where params should be written
        size_t paramsSize;        // Size of parameters to copy
        size_t paramsSizeAligned; // Aligned size for SBT alignment
    };

    uint32_t m_raygenTableSize;
    uint32_t m_missTableSize;
    uint32_t m_hitTableSize;
    uint32_t m_callableTableSize;

    uint32_t m_raygenRecordStride;
    uint32_t m_missRecordStride;
    uint32_t m_hitGroupRecordStride;
    uint32_t m_callableRecordStride;

    /// Information for each raygen shader.
    short_vector<RaygenInfo> m_raygenInfos;

    std::mutex m_mutex;
    std::map<RayTracingPipelineImpl*, RefPtr<BufferImpl>> m_buffers;

    ShaderTableImpl(Device* device, const ShaderTableDesc& desc);

    BufferImpl* getBuffer(RayTracingPipelineImpl* pipeline);
};

} // namespace rhi::vk
