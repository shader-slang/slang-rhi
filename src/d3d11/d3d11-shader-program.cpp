#include "d3d11-shader-program.h"
#include "d3d11-device.h"
#include "d3d11-shader-object-layout.h"

namespace rhi::d3d11 {

Result ShaderProgramImpl::createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode)
{
    m_modules.push_back({entryPointInfo->getStage(), kernelCode});
    return SLANG_OK;
}

ShaderObjectLayout* ShaderProgramImpl::getRootShaderObjectLayout()
{
    return m_rootObjectLayout;
}

ShaderProgramImpl::Module* ShaderProgramImpl::findModule(SlangStage stage)
{
    for (auto& module : m_modules)
    {
        if (module.stage == stage)
            return &module;
    }
    return nullptr;
}

} // namespace rhi::d3d11
