#pragma once

#include "cpu-base.h"

namespace rhi::cpu {

class PipelineStateImpl : public PipelineStateBase
{
public:
    ShaderProgramImpl* getProgram();

    void init(const ComputePipelineStateDesc& inDesc);
};

} // namespace rhi::cpu
