#pragma once

#include "d3d11-base.h"

#include "core/short_vector.h"

namespace rhi::d3d11 {

enum
{
    kMaxUAVs = 64,
    kMaxRTVs = 8,
};

class FramebufferLayoutImpl : public FramebufferLayoutBase
{
public:
    short_vector<TargetLayoutDesc> m_renderTargets;
    bool m_hasDepthStencil = false;
    TargetLayoutDesc m_depthStencil;
};

class FramebufferImpl : public FramebufferBase
{
public:
    short_vector<RefPtr<RenderTargetViewImpl>, kMaxRTVs> renderTargetViews;
    short_vector<ID3D11RenderTargetView*, kMaxRTVs> d3dRenderTargetViews;
    RefPtr<DepthStencilViewImpl> depthStencilView;
    ID3D11DepthStencilView* d3dDepthStencilView;
};

} // namespace rhi::d3d11
