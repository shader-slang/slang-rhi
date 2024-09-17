#pragma once

#include "metal-base.h"
#include "metal-device.h"

namespace rhi::metal {

class SamplerImpl : public Sampler
{
public:
    NS::SharedPtr<MTL::SamplerState> m_samplerState;

    SamplerImpl(const SamplerDesc& desc);
    ~SamplerImpl();

    Result init(DeviceImpl* device, const SamplerDesc& desc);

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::metal
