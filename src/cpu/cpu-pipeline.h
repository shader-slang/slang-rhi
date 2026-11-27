#pragma once

#include "cpu-base.h"

namespace rhi::cpu {

class ComputePipelineImpl : public ComputePipeline
{
public:
    ComPtr<ISlangSharedLibrary> m_sharedLibrary;
    slang_prelude::ComputeFunc m_func;
    /// Threads per thread group (the product of the [numthreads] dimensions).
    uint32_t m_threadGroupThreadCount = 1;

    ComputePipelineImpl(Device* device, const ComputePipelineDesc& desc);

    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::cpu
