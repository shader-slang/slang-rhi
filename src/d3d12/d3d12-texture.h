#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class TextureImpl : public Texture
{
public:
    TextureImpl(RendererBase* device, const TextureDesc& desc);

    ~TextureImpl();

    D3D12Resource m_resource;
    D3D12_RESOURCE_STATES m_defaultState;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;

public:
    struct ViewKey
    {
        Format format;
        TextureType type;
        SubresourceRange range;
        bool operator==(const ViewKey& other) const { return format == other.format && range == other.range; }
    };

    struct ViewKeyHasher
    {
        size_t operator()(const ViewKey& key) const
        {
            size_t hash = 0;
            hash_combine(hash, key.format);
            hash_combine(hash, key.type);
            hash_combine(hash, key.range.aspectMask);
            hash_combine(hash, key.range.baseArrayLayer);
            hash_combine(hash, key.range.layerCount);
            hash_combine(hash, key.range.mipLevel);
            hash_combine(hash, key.range.mipLevelCount);
            return hash;
        }
    };

    std::unordered_map<ViewKey, D3D12Descriptor, ViewKeyHasher> m_srvs;
    std::unordered_map<ViewKey, D3D12Descriptor, ViewKeyHasher> m_uavs;
    std::unordered_map<ViewKey, D3D12Descriptor, ViewKeyHasher> m_rtvs;
    std::unordered_map<ViewKey, D3D12Descriptor, ViewKeyHasher> m_dsvs;

    D3D12Descriptor getSRV(Format format, TextureType type, const SubresourceRange& range);
    D3D12Descriptor getUAV(Format format, TextureType type, const SubresourceRange& range);
    D3D12Descriptor getRTV(Format format, TextureType type, const SubresourceRange& range);
    D3D12Descriptor getDSV(Format format, TextureType type, const SubresourceRange& range);
};

} // namespace rhi::d3d12
