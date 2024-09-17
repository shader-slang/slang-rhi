#pragma once

#include "cuda-base.h"
#include "cuda-context.h"
#include "cuda-shader-object-layout.h"

namespace rhi::cuda {

class ShaderProgramImpl : public ShaderProgram
{
public:
    CUmodule cudaModule = nullptr;
    CUfunction cudaKernel;
    std::string kernelName;
    RefPtr<RootShaderObjectLayoutImpl> layout;
    RefPtr<CUDAContext> cudaContext;
    ~ShaderProgramImpl();
};

} // namespace rhi::cuda
