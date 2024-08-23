// cuda-shader-program.h
#pragma once
#include "cuda-base.h"

#include "cuda-context.h"
#include "cuda-shader-object-layout.h"

#include <string>

namespace gfx
{
#ifdef GFX_ENABLE_CUDA
using namespace Slang;

namespace cuda
{

class ShaderProgramImpl : public ShaderProgramBase
{
public:
    CUmodule cudaModule = nullptr;
    CUfunction cudaKernel;
    std::string kernelName;
    RefPtr<RootShaderObjectLayoutImpl> layout;
    RefPtr<CUDAContext> cudaContext;
    ~ShaderProgramImpl();
};

} // namespace cuda
#endif
} // namespace gfx
