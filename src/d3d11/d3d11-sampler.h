#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class SamplerImpl : public SamplerBase
{
public:
    SamplerImpl(RendererBase* device, const SamplerDesc& desc)
        : SamplerBase(device, desc)
    {
    }

    ComPtr<ID3D11SamplerState> m_sampler;
};

} // namespace rhi::d3d11
