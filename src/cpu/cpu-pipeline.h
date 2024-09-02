#pragma once

#include "cpu-base.h"

namespace rhi::cpu {

class ComputePipelineImpl : public ComputePipelineBase
{
public:
    ShaderProgramImpl* getProgram();

    Result init(const ComputePipelineDesc& desc);
};

} // namespace rhi::cpu
