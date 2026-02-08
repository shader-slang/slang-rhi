#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class SamplerImpl : public Sampler
{
public:
    SamplerImpl(Device* device, const SamplerDesc& desc);
    ~SamplerImpl();

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

public:
    WGPUSampler m_sampler = nullptr;
};

} // namespace rhi::wgpu
