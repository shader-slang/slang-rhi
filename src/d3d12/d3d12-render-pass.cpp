#include "d3d12-render-pass.h"

namespace rhi::d3d12 {

void RenderPassLayoutImpl::init(const IRenderPassLayout::Desc& desc)
{
    SimpleRenderPassLayout::init(desc);
    m_framebufferLayout = static_cast<FramebufferLayoutImpl*>(desc.framebufferLayout);
    m_hasDepthStencil = m_framebufferLayout->m_hasDepthStencil;
}

} // namespace rhi::d3d12
