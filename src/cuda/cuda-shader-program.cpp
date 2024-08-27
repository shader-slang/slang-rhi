// cuda-shader-program.cpp
#include "cuda-shader-program.h"

namespace rhi
{
using namespace Slang;

namespace cuda
{

ShaderProgramImpl::~ShaderProgramImpl()
{
    if (cudaModule)
        cuModuleUnload(cudaModule);
}

} // namespace cuda
} // namespace rhi
