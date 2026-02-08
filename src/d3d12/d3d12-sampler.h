#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class SamplerImpl : public Sampler
{
public:
    SamplerImpl(Device* device, const SamplerDesc& desc);
    ~SamplerImpl();

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // ISampler implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(DescriptorHandle* outHandle) override;

public:
    CPUDescriptorAllocation m_descriptor;
    DescriptorHandle m_descriptorHandle;
};

} // namespace rhi::d3d12
