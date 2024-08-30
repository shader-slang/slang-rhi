#include "cuda-shader-program.h"

namespace rhi::cuda {

ShaderProgramImpl::~ShaderProgramImpl()
{
    if (cudaModule)
        cuModuleUnload(cudaModule);
}

} // namespace rhi::cuda
