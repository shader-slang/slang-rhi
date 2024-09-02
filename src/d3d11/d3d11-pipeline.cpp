#include "d3d11-pipeline.h"

namespace rhi::d3d11 {

void GraphicsPipelineImpl::init(const RenderPipelineDesc& inDesc)
{
    PipelineBase::PipelineStateDesc pipelineDesc;
    pipelineDesc.graphics = inDesc;
    pipelineDesc.type = PipelineType::Graphics;
    initializeBase(pipelineDesc);
}

void ComputePipelineImpl::init(const ComputePipelineDesc& inDesc)
{
    PipelineBase::PipelineStateDesc pipelineDesc;
    pipelineDesc.compute = inDesc;
    pipelineDesc.type = PipelineType::Compute;
    initializeBase(pipelineDesc);
}

} // namespace rhi::d3d11
