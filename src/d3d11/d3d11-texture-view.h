#pragma once

#include "d3d11-base.h"
#include "d3d11-texture.h"

namespace rhi::d3d11 {

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(const TextureViewDesc& desc)
        : TextureView(desc)
    {
    }

    ID3D11RenderTargetView* getRTV()
    {
        if (!m_rtv)
            m_rtv = m_texture->getRTV(m_desc.format, m_desc.subresourceRange);
        return m_rtv;
    }

    ID3D11DepthStencilView* getDSV()
    {
        if (!m_dsv)
            m_dsv = m_texture->getDSV(m_desc.format, m_desc.subresourceRange);
        return m_dsv;
    }

    ID3D11ShaderResourceView* getSRV()
    {
        if (!m_srv)
            m_srv = m_texture->getSRV(m_desc.format, m_desc.subresourceRange);
        return m_srv;
    }

    ID3D11UnorderedAccessView* getUAV()
    {
        if (!m_uav)
            m_uav = m_texture->getUAV(m_desc.format, m_desc.subresourceRange);
        return m_uav;
    }

public:
    RefPtr<TextureImpl> m_texture;

private:
    ID3D11RenderTargetView* m_rtv = nullptr;
    ID3D11DepthStencilView* m_dsv = nullptr;
    ID3D11ShaderResourceView* m_srv = nullptr;
    ID3D11UnorderedAccessView* m_uav = nullptr;
};

} // namespace rhi::d3d11
