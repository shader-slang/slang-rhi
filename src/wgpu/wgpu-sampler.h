#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class SamplerImpl : public Sampler
{
public:
    DeviceImpl* m_device;
    WGPUSampler m_sampler = nullptr;

    SamplerImpl(DeviceImpl* device, const SamplerDesc& desc);
    ~SamplerImpl();

    // ISampler implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::wgpu
