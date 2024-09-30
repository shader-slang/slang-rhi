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

    std::unordered_map<ViewKey, ComPtr<ID3D11RenderTargetView>, ViewKeyHasher> m_rtvs;
    std::unordered_map<ViewKey, ComPtr<ID3D11DepthStencilView>, ViewKeyHasher> m_dsvs;
    std::unordered_map<ViewKey, ComPtr<ID3D11ShaderResourceView>, ViewKeyHasher> m_srvs;
    std::unordered_map<ViewKey, ComPtr<ID3D11UnorderedAccessView>, ViewKeyHasher> m_uavs;

    ID3D11RenderTargetView* getRTV(Format format, const SubresourceRange& range);
    ID3D11DepthStencilView* getDSV(Format format, const SubresourceRange& range);
    ID3D11ShaderResourceView* getSRV(Format format, const SubresourceRange& range);
    ID3D11UnorderedAccessView* getUAV(Format format, const SubresourceRange& range);
};

} // namespace rhi::d3d11
