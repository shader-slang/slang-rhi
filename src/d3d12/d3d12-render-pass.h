#pragma once

#include "d3d12-base.h"
#include "d3d12-framebuffer.h"

namespace rhi::d3d12 {

class RenderPassLayoutImpl : public SimpleRenderPassLayout
{
public:
    RefPtr<FramebufferLayoutImpl> m_framebufferLayout;
    void init(const IRenderPassLayout::Desc& desc);
};

} // namespace rhi::d3d12
