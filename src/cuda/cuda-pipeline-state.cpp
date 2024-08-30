#include "cuda-pipeline-state.h"

namespace rhi::cuda {

void ComputePipelineStateImpl::init(const ComputePipelineStateDesc& inDesc)
{
    PipelineStateDesc pipelineDesc;
    pipelineDesc.type = PipelineType::Compute;
    pipelineDesc.compute = inDesc;
    initializeBase(pipelineDesc);
}

} // namespace rhi::cuda
