// cuda-shader-program.cpp
#include "cuda-shader-program.h"

namespace gfx
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
} // namespace gfx
