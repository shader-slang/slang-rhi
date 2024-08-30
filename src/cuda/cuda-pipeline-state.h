#pragma once

#include "cuda-base.h"
#include "cuda-shader-program.h"

namespace rhi::cuda {

class PipelineStateImpl : public PipelineStateBase
{
public:
};

class ComputePipelineStateImpl : public PipelineStateImpl
{
public:
    RefPtr<ShaderProgramImpl> shaderProgram;
    void init(const ComputePipelineStateDesc& inDesc);
};

} // namespace rhi::cuda
