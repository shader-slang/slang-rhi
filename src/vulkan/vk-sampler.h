#pragma once

#include "vk-base.h"
#include "vk-device.h"

namespace rhi::vk {

class SamplerImpl : public SamplerBase
{
public:
    VkSampler m_sampler;
    RefPtr<DeviceImpl> m_device;
    SamplerImpl(DeviceImpl* device);
    ~SamplerImpl();
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(InteropHandle* outHandle) override;
};

} // namespace rhi::vk
