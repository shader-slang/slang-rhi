#include "cuda-pipeline.h"
#include "cuda-device.h"

namespace rhi::cuda {

ComputePipelineImpl::~ComputePipelineImpl()
{
    if (m_module)
        cuModuleUnload(m_module);
}

Result ComputePipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::CUmodule;
    outHandle->value = (uint64_t)m_module;
    return SLANG_OK;
}

Result DeviceImpl::createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    ShaderProgramImpl* program = checked_cast<ShaderProgramImpl*>(desc.program);
    const auto& module = program->m_modules[0];

    RefPtr<ComputePipelineImpl> pipeline = new ComputePipelineImpl();
    pipeline->m_program = program;
    pipeline->m_rootObjectLayout = program->m_rootObjectLayout;

    SLANG_CUDA_RETURN_ON_FAIL(cuModuleLoadData(&pipeline->m_module, module.code->getBufferPointer()));
    pipeline->m_kernelName = module.entryPointName;
    SLANG_CUDA_RETURN_ON_FAIL(
        cuModuleGetFunction(&pipeline->m_function, pipeline->m_module, pipeline->m_kernelName.data())
    );

    pipeline->m_program = checked_cast<ShaderProgram*>(desc.program);
    pipeline->m_programImpl = checked_cast<ShaderProgramImpl*>(desc.program);
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

} // namespace rhi::cuda
