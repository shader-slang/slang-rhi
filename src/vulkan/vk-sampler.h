#pragma once

#include "vk-base.h"

namespace rhi::vk {

class SamplerImpl : public Sampler
{
public:
    SamplerImpl(Device* device, const SamplerDesc& desc);
    ~SamplerImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(DescriptorHandle* outHandle) override;

public:
    VkSampler m_sampler;
    DescriptorHandle m_descriptorHandle;
};

} // namespace rhi::vk
