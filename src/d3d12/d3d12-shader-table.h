#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class ShaderTableImpl : public ShaderTable
{
public:
    /// Data specific to a pipeline, including the buffer and table offsets/strides.
    struct PipelineData : public RefObject
    {
        /// Native shader table containing identifiers and local-root arguments.
        RefPtr<BufferImpl> buffer;

        /// RHI-owned application bytes addressed by structural Record root CBVs.
        RefPtr<BufferImpl> structuralRecordBuffer;

        Size rayGenTableOffset;
        Size missTableOffset;
        Size hitGroupTableOffset;
        Size callableTableOffset;

        Size missTableSize;
        Size hitGroupTableSize;
        Size callableTableSize;

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
