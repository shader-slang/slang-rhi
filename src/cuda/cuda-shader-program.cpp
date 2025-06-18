#include "cuda-shader-program.h"
#include "cuda-shader-object-layout.h"

namespace rhi::cuda {

ShaderProgramImpl::ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc)
    : ShaderProgram(device, desc)
{
}

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

ShaderObjectLayout* ShaderProgramImpl::getRootShaderObjectLayout()
{
    return m_rootObjectLayout;
}

} // namespace rhi::cuda
