#pragma once

#include "cuda-base.h"
#include "cuda-shader-program.h"

namespace rhi::cuda {

class ComputePipelineImpl : public ComputePipelineBase
{
public:
    Result init(const ComputePipelineDesc& desc);
};

} // namespace rhi::cuda
