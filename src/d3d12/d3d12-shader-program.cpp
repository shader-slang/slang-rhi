#include "d3d12-shader-program.h"
#include "d3d12-shader-object-layout.h"

namespace rhi::d3d12 {

ShaderProgramImpl::ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc)
    : ShaderProgram(device, desc)
{
}

Result ShaderProgramImpl::createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode)
{
    ShaderBinary shaderBin;
    shaderBin.stage = entryPointInfo->getStage();
    shaderBin.entryPointInfo = entryPointInfo;
    shaderBin.code.assign(
        reinterpret_cast<const uint8_t*>(kernelCode->getBufferPointer()),
        reinterpret_cast<const uint8_t*>(kernelCode->getBufferPointer()) + (size_t)kernelCode->getBufferSize()
    );
    m_shaders.push_back(_Move(shaderBin));
    return SLANG_OK;
}

ShaderObjectLayout* ShaderProgramImpl::getRootShaderObjectLayout()
{
    return m_rootObjectLayout;
}

} // namespace rhi::d3d12
