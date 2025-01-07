#include "cuda-shader-program.h"

namespace rhi::cuda {

ShaderProgramImpl::~ShaderProgramImpl() {}

Result ShaderProgramImpl::createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode)
{
    Module module;
    module.stage = entryPointInfo->getStage();
    module.entryPointName = entryPointInfo->getNameOverride();
    module.code = kernelCode;
    m_modules.push_back(module);
    return SLANG_OK;
}

} // namespace rhi::cuda
