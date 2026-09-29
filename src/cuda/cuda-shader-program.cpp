#include "cuda-shader-program.h"
#include "cuda-shader-object-layout.h"

namespace rhi::cuda {

ShaderProgramImpl::ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc)
    : ShaderProgram(device, desc)
{
}

ShaderProgramImpl::~ShaderProgramImpl() {}

Result ShaderProgramImpl::createShaderModule(const ShaderModuleDesc& desc, ComPtr<ISlangBlob> kernelCode)
{
    Module module;
    module.stage = desc.stage;
    module.entryPointName = desc.entryPointName;
    module.code = kernelCode;
    m_modules.push_back(module);
    return SLANG_OK;
}

ShaderObjectLayout* ShaderProgramImpl::getRootShaderObjectLayout()
{
    return m_rootObjectLayout;
}

} // namespace rhi::cuda
