#pragma once

#include "metal-base.h"

namespace rhi::metal {

class SamplerImpl : public Sampler
{
public:
    NS::SharedPtr<MTL::SamplerState> m_samplerState;

    SamplerImpl(Device* device, const SamplerDesc& desc);
    ~SamplerImpl();

    Result init();

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::metal
