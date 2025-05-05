#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class SamplerImpl : public Sampler
{
public:
    CPUDescriptorAllocation m_descriptor;
    DescriptorHandle m_descriptorHandle;

    SamplerImpl(Device* device, const SamplerDesc& desc);
    ~SamplerImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(DescriptorHandle* outHandle) override;
};

} // namespace rhi::d3d12
