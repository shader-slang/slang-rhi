#pragma once

#include "webgpu-base.h"

namespace rhi::webgpu {

class SamplerImpl : public Sampler
{
public:
    WebGPUSampler m_sampler = nullptr;

    SamplerImpl(Device* device, const SamplerDesc& desc);
    ~SamplerImpl();

    // ISampler implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::webgpu
