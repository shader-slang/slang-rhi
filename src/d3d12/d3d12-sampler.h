#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class SamplerImpl : public Sampler
{
public:
    DeviceImpl* m_device;
    CPUDescriptorAllocation m_descriptor;

    SamplerImpl(const SamplerDesc& desc)
        : Sampler(desc)
    {
    }

    ~SamplerImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::d3d12
