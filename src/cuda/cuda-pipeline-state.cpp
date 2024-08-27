// cuda-pipeline-state.cpp
#include "cuda-pipeline-state.h"

namespace rhi
{
using namespace Slang;

namespace cuda
{

void ComputePipelineStateImpl::init(const ComputePipelineStateDesc& inDesc)
{
    PipelineStateDesc pipelineDesc;
    pipelineDesc.type = PipelineType::Compute;
    pipelineDesc.compute = inDesc;
    initializeBase(pipelineDesc);
}

} // namespace cuda
} // namespace rhi
