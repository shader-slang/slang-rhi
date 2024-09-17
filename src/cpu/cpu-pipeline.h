#pragma once

#include "cpu-base.h"

namespace rhi::cpu {

class PipelineImpl : public Pipeline
{
public:
    ShaderProgramImpl* getProgram();

    void init(const ComputePipelineDesc& inDesc);
};

} // namespace rhi::cpu
