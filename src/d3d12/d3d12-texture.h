#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class TextureImpl : public Texture
{
public:
    TextureImpl(DeviceImpl* device, const TextureDesc& desc);

    ~TextureImpl();

    DeviceImpl* m_device;
    D3D12Resource m_resource;
    D3D12_RESOURCE_STATES m_defaultState;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;

public:
    struct ViewKey
    {
        Format format;
        TextureType type;
        TextureAspect aspect;
        SubresourceRange range;
        bool operator==(const ViewKey& other) const
        {
            return format == other.format && type == other.type && aspect == other.aspect && range == other.range;
        }
    };

    struct ViewKeyHasher
    {
        size_t operator()(const ViewKey& key) const
        {
            size_t hash = 0;
            hash_combine(hash, key.format);
            hash_combine(hash, key.type);
            hash_combine(hash, key.aspect);
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

    D3D12Descriptor getSRV(Format format, TextureType type, TextureAspect aspect, const SubresourceRange& range);
    D3D12Descriptor getUAV(Format format, TextureType type, TextureAspect aspect, const SubresourceRange& range);
    D3D12Descriptor getRTV(Format format, TextureType type, TextureAspect aspect, const SubresourceRange& range);
    D3D12Descriptor getDSV(Format format, TextureType type, TextureAspect aspect, const SubresourceRange& range);
};

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(const TextureViewDesc& desc)
        : TextureView(desc)
    {
    }

    RefPtr<TextureImpl> m_texture;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    D3D12Descriptor getSRV();
    D3D12Descriptor getUAV();
    D3D12Descriptor getRTV();
    D3D12Descriptor getDSV();

private:
    D3D12Descriptor m_srv = {};
    D3D12Descriptor m_uav = {};
    D3D12Descriptor m_rtv = {};
    D3D12Descriptor m_dsv = {};
};

} // namespace rhi::d3d12
