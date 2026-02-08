#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class TextureImpl : public Texture
{
public:
    TextureImpl(Device* device, const TextureDesc& desc);
    ~TextureImpl();

    virtual void deleteThis() override;

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // ITexture implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDefaultView(ITextureView** outTextureView) override;

public:
    D3D12Resource m_resource;
    DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
    bool m_isTypeless = false;
    D3D12_RESOURCE_STATES m_defaultState = D3D12_RESOURCE_STATE_COMMON;
    // True if this texture is created from a swap chain buffer.
    // Swap chain textures are deleted immediately when deleteThis() is called.
    bool m_isSwapchainTexture = false;
    RefPtr<TextureViewImpl> m_defaultView;

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
            hash_combine(hash, key.range.layer);
            hash_combine(hash, key.range.layerCount);
            hash_combine(hash, key.range.mip);
            hash_combine(hash, key.range.mipCount);
            return hash;
        }
    };

    std::unordered_map<ViewKey, CPUDescriptorAllocation, ViewKeyHasher> m_srvs;
    std::unordered_map<ViewKey, CPUDescriptorAllocation, ViewKeyHasher> m_uavs;
    std::unordered_map<ViewKey, CPUDescriptorAllocation, ViewKeyHasher> m_rtvs;
    std::unordered_map<ViewKey, CPUDescriptorAllocation, ViewKeyHasher> m_dsvs;

    D3D12_CPU_DESCRIPTOR_HANDLE getSRV(
        Format format,
        TextureType type,
        TextureAspect aspect,
        const SubresourceRange& range
    );
    D3D12_CPU_DESCRIPTOR_HANDLE getUAV(
        Format format,
        TextureType type,
        TextureAspect aspect,
        const SubresourceRange& range
    );
    D3D12_CPU_DESCRIPTOR_HANDLE getRTV(
        Format format,
        TextureType type,
        TextureAspect aspect,
        const SubresourceRange& range
    );
    D3D12_CPU_DESCRIPTOR_HANDLE getDSV(
        Format format,
        TextureType type,
        TextureAspect aspect,
        const SubresourceRange& range
    );
};

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(Device* device, const TextureViewDesc& desc);
    ~TextureViewImpl();

    // RefObject implementation
    virtual void makeExternal() override { m_texture.establishStrongReference(); }
    virtual void makeInternal() override { m_texture.breakStrongReference(); }

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // ITextureView implementation
    virtual SLANG_NO_THROW ITexture* SLANG_MCALL getTexture() override { return m_texture; }
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(
        DescriptorHandleAccess access,
        DescriptorHandle* outHandle
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getCombinedTextureSamplerDescriptorHandle(
        DescriptorHandle* outHandle
    ) override;

public:
    D3D12_CPU_DESCRIPTOR_HANDLE getSRV();
    D3D12_CPU_DESCRIPTOR_HANDLE getUAV();
    D3D12_CPU_DESCRIPTOR_HANDLE getRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE getDSV();

    BreakableReference<TextureImpl> m_texture;
    DescriptorHandle m_descriptorHandle[2] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_srv = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_uav = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtv = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_dsv = {};
};

} // namespace rhi::d3d12
