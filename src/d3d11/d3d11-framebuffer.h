// d3d11-framebuffer.h
#pragma once

#include "d3d11-base.h"

#include "utils/short_vector.h"

namespace rhi
{

using namespace Slang;

namespace d3d11
{

enum
{
    kMaxUAVs = 64,
    kMaxRTVs = 8,
};

class FramebufferLayoutImpl : public FramebufferLayoutBase
{
public:
    short_vector<IFramebufferLayout::TargetLayout> m_renderTargets;
    bool m_hasDepthStencil = false;
    IFramebufferLayout::TargetLayout m_depthStencil;
};

class FramebufferImpl : public FramebufferBase
{
public:
    short_vector<RefPtr<RenderTargetViewImpl>, kMaxRTVs> renderTargetViews;
    short_vector<ID3D11RenderTargetView*, kMaxRTVs> d3dRenderTargetViews;
    RefPtr<DepthStencilViewImpl> depthStencilView;
    ID3D11DepthStencilView* d3dDepthStencilView;
};

} // namespace d3d11
} // namespace rhi
