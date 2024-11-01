#pragma once

#include "cuda-base.h"
#include "cuda-shader-program.h"

namespace rhi::cuda {

class ComputePipelineImpl : public ComputePipeline
{
public:
    RefPtr<ShaderProgramImpl> m_programImpl;

    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::cuda
