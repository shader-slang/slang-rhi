#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class ShaderTableImpl : public ShaderTable
{
public:
    /// Data specific to a pipeline, including the buffer and table offsets/strides.
    struct PipelineData : public RefObject
    {
        RefPtr<BufferImpl> buffer;

        uint32_t rayGenTableOffset;
        uint32_t missTableOffset;
        uint32_t hitGroupTableOffset;
        uint32_t callableTableOffset;

        uint32_t rayGenRecordStride;
        uint32_t missRecordStride;
        uint32_t hitGroupRecordStride;
        uint32_t callableRecordStride;
    };

    std::mutex m_mutex;
    std::map<RayTracingPipelineImpl*, RefPtr<PipelineData>> m_pipelineData;

    ShaderTableImpl(Device* device, const ShaderTableDesc& desc);

    PipelineData* getPipelineData(RayTracingPipelineImpl* pipeline);
};

} // namespace rhi::d3d12
