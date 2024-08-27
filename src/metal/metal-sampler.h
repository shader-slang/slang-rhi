// metal-sampler.h
#pragma once

#include "metal-base.h"
#include "metal-device.h"

namespace rhi
{

using namespace Slang;

namespace metal
{

class SamplerStateImpl : public SamplerStateBase
{
public:
    RefPtr<DeviceImpl> m_device;
    NS::SharedPtr<MTL::SamplerState> m_samplerState;

    ~SamplerStateImpl();

    Result init(DeviceImpl* device, const ISamplerState::Desc& desc);
    
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(InteropHandle* outHandle) override;
};

} // namespace metal
} // namespace rhi
