// cuda-pipeline-state.h
#pragma once
#include "cuda-base.h"
#include "cuda-shader-program.h"

namespace gfx
{
using namespace Slang;

namespace cuda
{

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

} // namespace cuda
} // namespace gfx
