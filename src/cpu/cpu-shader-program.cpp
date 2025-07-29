#include "cpu-shader-program.h"
#include "cpu-shader-object-layout.h"

namespace rhi::cpu {

ShaderProgramImpl::ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc)
    : ShaderProgram(device, desc)
{
}

ShaderProgramImpl::~ShaderProgramImpl() {}

ShaderObjectLayout* ShaderProgramImpl::getRootShaderObjectLayout()
{
    return m_rootShaderObjectLayout;
}

} // namespace rhi::cpu
