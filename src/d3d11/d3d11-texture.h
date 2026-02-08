#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class TextureImpl : public Texture
{
public:
    TextureImpl(Device* device, const TextureDesc& desc);
    ~TextureImpl();

    // ITexture implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getDefaultView(ITextureView** outTextureView) override;

public:
    ID3D11RenderTargetView* getRTV(Format format, const SubresourceRange& range);
    ID3D11DepthStencilView* getDSV(Format format, const SubresourceRange& range);
    ID3D11ShaderResourceView* getSRV(Format format, const SubresourceRange& range);
    ID3D11UnorderedAccessView* getUAV(Format format, const SubresourceRange& range);

    ComPtr<ID3D11Resource> m_resource;
    DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
    bool m_isTypeless = false;
    RefPtr<TextureViewImpl> m_defaultView;


    struct ViewKey
    {
        Format format;
        SubresourceRange range;
        bool operator==(const ViewKey& other) const { return format == other.format && range == other.range; }
    };

    struct ViewKeyHasher
    {
        size_t operator()(const ViewKey& key) const
        {
            size_t hash = 0;
            hash_combine(hash, key.format);
            hash_combine(hash, key.range.layer);
            hash_combine(hash, key.range.layerCount);
            hash_combine(hash, key.range.mip);
            hash_combine(hash, key.range.mipCount);
            return hash;
        }
    };

    std::unordered_map<ViewKey, ComPtr<ID3D11RenderTargetView>, ViewKeyHasher> m_rtvs;
    std::unordered_map<ViewKey, ComPtr<ID3D11DepthStencilView>, ViewKeyHasher> m_dsvs;
    std::unordered_map<ViewKey, ComPtr<ID3D11ShaderResourceView>, ViewKeyHasher> m_srvs;
    std::unordered_map<ViewKey, ComPtr<ID3D11UnorderedAccessView>, ViewKeyHasher> m_uavs;
};

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(Device* device, const TextureViewDesc& desc);

    // RefObject implementation
    virtual void makeExternal() override { m_texture.establishStrongReference(); }
    virtual void makeInternal() override { m_texture.breakStrongReference(); }

    // ITextureView implementation
    virtual SLANG_NO_THROW ITexture* SLANG_MCALL getTexture() override { return m_texture; }

public:
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

    BreakableReference<TextureImpl> m_texture;
    ID3D11RenderTargetView* m_rtv = nullptr;
    ID3D11DepthStencilView* m_dsv = nullptr;
    ID3D11ShaderResourceView* m_srv = nullptr;
    ID3D11UnorderedAccessView* m_uav = nullptr;
};

} // namespace rhi::d3d11
