#pragma once

#include "vk-base.h"
#include "vk-device.h"

namespace rhi::vk {

class SamplerImpl : public Sampler
{
public:
    SamplerImpl(DeviceImpl* device, const SamplerDesc& desc);
    ~SamplerImpl();
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

public:
    DeviceImpl* m_device;
    VkSampler m_sampler;
};

} // namespace rhi::vk
