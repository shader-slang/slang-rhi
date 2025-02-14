#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class TextureImpl : public Texture
{
public:
    TextureImpl(DeviceImpl* device, const TextureDesc& desc)
        : Texture(desc)
        , m_device(device)
    {
    }

    DeviceImpl* m_device;
    ComPtr<ID3D11Resource> m_resource;

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
            hash_combine(hash, key.range.baseArrayLayer);
            hash_combine(hash, key.range.layerCount);
            hash_combine(hash, key.range.mipLevel);
            hash_combine(hash, key.range.mipLevelCount);
            return hash;
        }
    };

    std::mutex m_mutex;
    std::unordered_map<ViewKey, ComPtr<ID3D11RenderTargetView>, ViewKeyHasher> m_rtvs;
    std::unordered_map<ViewKey, ComPtr<ID3D11DepthStencilView>, ViewKeyHasher> m_dsvs;
    std::unordered_map<ViewKey, ComPtr<ID3D11ShaderResourceView>, ViewKeyHasher> m_srvs;
    std::unordered_map<ViewKey, ComPtr<ID3D11UnorderedAccessView>, ViewKeyHasher> m_uavs;

    ID3D11RenderTargetView* getRTV(Format format, const SubresourceRange& range);
    ID3D11DepthStencilView* getDSV(Format format, const SubresourceRange& range);
    ID3D11ShaderResourceView* getSRV(Format format, const SubresourceRange& range);
    ID3D11UnorderedAccessView* getUAV(Format format, const SubresourceRange& range);
};

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
