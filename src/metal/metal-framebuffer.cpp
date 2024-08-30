#include "metal-framebuffer.h"
#include "metal-device.h"
#include "metal-helper-functions.h"
#include "metal-resource-views.h"

namespace rhi::metal {

Result FramebufferLayoutImpl::init(const IFramebufferLayout::Desc& desc)
{
    for (Index i = 0; i < desc.renderTargetCount; ++i)
    {
        m_renderTargets.push_back(desc.renderTargets[i]);
    }
    if (desc.depthStencil)
    {
        m_depthStencil = *desc.depthStencil;
    }
    else
    {
        m_depthStencil = {};
    }
    return SLANG_OK;
}

Result FramebufferImpl::init(DeviceImpl* device, const IFramebuffer::Desc& desc)
{
    m_device = device;
    m_layout = static_cast<FramebufferLayoutImpl*>(desc.layout);
    m_renderTargetViews.resize(desc.renderTargetCount);
    for (Index i = 0; i < desc.renderTargetCount; ++i)
    {
        m_renderTargetViews[i] = static_cast<TextureViewImpl*>(desc.renderTargetViews[i]);
    }
    m_depthStencilView = static_cast<TextureViewImpl*>(desc.depthStencilView);

    // Determine framebuffer dimensions & sample count;
    m_width = 1;
    m_height = 1;
    m_sampleCount = 1;

    auto visitView = [this](TextureViewImpl* view)
    {
        const TextureDesc* textureDesc = view->m_texture->getDesc();
        const IResourceView::Desc* viewDesc = view->getViewDesc();
        m_width = std::max(1u, uint32_t(textureDesc->size.width >> viewDesc->subresourceRange.mipLevel));
        m_height = std::max(1u, uint32_t(textureDesc->size.height >> viewDesc->subresourceRange.mipLevel));
        m_sampleCount = std::max(m_sampleCount, uint32_t(textureDesc->sampleCount));
        return SLANG_OK;
    };

    for (auto view : m_renderTargetViews)
    {
        visitView(view);
    }
    if (m_depthStencilView)
    {
        visitView(m_depthStencilView);
    }

    return SLANG_OK;
}

} // namespace rhi::metal
