#pragma once

#include "vk-base.h"

namespace rhi::vk {

class SamplerImpl : public Sampler
{
public:
    SamplerImpl(Device* device, const SamplerDesc& desc);
    ~SamplerImpl();

    virtual void deleteThis() override;

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // ISampler implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(DescriptorHandle* outHandle) override;

public:
    VkSampler m_sampler;
    uint32_t m_customBorderColorIndex = UINT32_MAX;
    std::vector<uint8_t> m_heapDescriptor;
    DescriptorHandle m_descriptorHandle;
};

} // namespace rhi::vk
