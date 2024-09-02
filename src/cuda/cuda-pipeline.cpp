#include "cuda-pipeline.h"

namespace rhi::cuda {

Result ComputePipelineImpl::init(const ComputePipelineDesc& desc)
{
    return ComputePipelineBase::init(desc);
}

} // namespace rhi::cuda
