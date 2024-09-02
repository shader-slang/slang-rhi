#include "d3d11-pipeline.h"

namespace rhi::d3d11 {

Result RenderPipelineImpl::init(const RenderPipelineDesc& desc)
{
    return RenderPipelineBase::init(desc);
}

Result ComputePipelineImpl::init(const ComputePipelineDesc& desc)
{
    return ComputePipelineBase::init(desc);
}

} // namespace rhi::d3d11
