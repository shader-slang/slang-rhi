#include "cuda-pipeline.h"
#include "cuda-device.h"

namespace rhi::cuda {

Result ComputePipelineImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result DeviceImpl::createComputePipeline2(const ComputePipelineDesc2& desc, IComputePipeline** outPipeline)
{
    RefPtr<ComputePipelineImpl> pipeline = new ComputePipelineImpl();
    pipeline->m_program = checked_cast<ShaderProgramImpl*>(desc.program);
    returnComPtr(outPipeline, pipeline);
    return SLANG_OK;
}

} // namespace rhi::cuda
