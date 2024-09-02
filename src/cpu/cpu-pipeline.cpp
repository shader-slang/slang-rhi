#include "cpu-pipeline.h"

#include "cpu-shader-program.h"

namespace rhi::cpu {

ShaderProgramImpl* ComputePipelineImpl::getProgram()
{
    return static_cast<ShaderProgramImpl*>(m_program.Ptr());
}

Result ComputePipelineImpl::init(const ComputePipelineDesc& desc)
{
    SLANG_RETURN_ON_FAIL(ComputePipelineBase::init(desc));
    ComputePipelineBase::init(desc);
}

} // namespace rhi::cpu
