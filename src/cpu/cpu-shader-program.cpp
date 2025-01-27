#include "cpu-shader-program.h"
#include "cpu-shader-object-layout.h"

namespace rhi::cpu {

ShaderObjectLayout* ShaderProgramImpl::getRootShaderObjectLayout()
{
    return m_rootShaderObjectLayout;
}

} // namespace rhi::cpu
