#pragma once

#include "d3d12-base.h"
#include "d3d12-resource-views.h"

#include "utils/short_vector.h"

namespace rhi::d3d12 {

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
    short_vector<RefPtr<ResourceViewImpl>> renderTargetViews;
    RefPtr<ResourceViewImpl> depthStencilView;
    short_vector<D3D12_CPU_DESCRIPTOR_HANDLE> renderTargetDescriptors;
    struct Color4f
    {
        float values[4];
    };
    short_vector<Color4f> renderTargetClearValues;
    D3D12_CPU_DESCRIPTOR_HANDLE depthStencilDescriptor;
    DepthStencilClearValue depthStencilClearValue;
};

} // namespace rhi::d3d12
