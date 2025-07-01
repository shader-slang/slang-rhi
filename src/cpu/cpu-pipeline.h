#pragma once

#include "cpu-base.h"

namespace rhi::cpu {

class ComputePipelineImpl : public ComputePipeline
{
public:
    ComPtr<ISlangSharedLibrary> m_sharedLibrary;
    slang_prelude::ComputeFunc m_func;

    ComputePipelineImpl(Device* device, const ComputePipelineDesc& desc);

    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::cpu
