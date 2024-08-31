#include "cuda-pipeline-state.h"

namespace rhi::cuda {

void ComputePipelineImpl::init(const ComputePipelineDesc& inDesc)
{
    PipelineStateDesc pipelineDesc;
    pipelineDesc.type = PipelineType::Compute;
    pipelineDesc.compute = inDesc;
    initializeBase(pipelineDesc);
}

} // namespace rhi::cuda
