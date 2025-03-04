#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class SamplerImpl : public Sampler
{
public:
    SamplerImpl(Device* device, const SamplerDesc& desc);

    ComPtr<ID3D11SamplerState> m_sampler;
};

} // namespace rhi::d3d11
