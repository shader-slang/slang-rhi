#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class SamplerStateImpl : public SamplerStateBase
{
public:
    ComPtr<ID3D11SamplerState> m_sampler;
};

} // namespace rhi::d3d11
