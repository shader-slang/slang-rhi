#pragma once

#include "cuda-base.h"
#include "cuda-shader-program.h"

namespace rhi::cuda {

class PipelineImpl : public PipelineBase
{
public:
};

class ComputePipelineImpl : public PipelineImpl
{
public:
    RefPtr<ShaderProgramImpl> shaderProgram;
    void init(const ComputePipelineDesc& inDesc);
};

} // namespace rhi::cuda
