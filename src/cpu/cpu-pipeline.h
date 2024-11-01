#pragma once

#include "cpu-base.h"

namespace rhi::cpu {

class ComputePipelineImpl : public ComputePipeline
{
public:
    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::cpu
