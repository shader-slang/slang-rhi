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
        /// Index into the root object layout's entry points.
        uint32_t entryPointIndex;
        /// Offset within the SBT buffer where params should be written.
        uint64_t sbtOffset;
        /// Size of parameters to copy.
        size_t paramsSize;
        /// Offset of this raygen record from the start of the raygen table.
        uint32_t recordOffset;
        /// Aligned size of this raygen record.
        uint32_t recordSize;
    };

    /// Data specific to a pipeline, including the buffer and raygen infos.
    struct PipelineData : public RefObject
    {
        RefPtr<BufferImpl> buffer;
        short_vector<RaygenInfo> raygenInfos;

        uint32_t raygenTableSize;
        uint32_t missTableSize;
        uint32_t hitTableSize;
        uint32_t callableTableSize;

        uint32_t missRecordStride;
        uint32_t hitGroupRecordStride;
        uint32_t callableRecordStride;
    };

    std::mutex m_mutex;
    std::map<RayTracingPipelineImpl*, RefPtr<PipelineData>> m_pipelineData;

    ShaderTableImpl(Device* device, const ShaderTableDesc& desc);

    PipelineData* getPipelineData(RayTracingPipelineImpl* pipeline);
};

} // namespace rhi::vk
