#pragma once

#include "vk-base.h"
#include "vk-device.h"

namespace rhi::vk {

class SamplerImpl : public SamplerBase
{
public:
    SamplerImpl(RendererBase* device, const SamplerDesc& desc);
    ~SamplerImpl();
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

public:
    VkSampler m_sampler;
};

} // namespace rhi::vk
