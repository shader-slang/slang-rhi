#pragma once

#include "cuda-base.h"
#include "cuda-sampler.h"

namespace rhi::cuda {

bool isFormatSupported(Format format);

class TextureImpl : public Texture
{
public:
    TextureImpl(Device* device, const TextureDesc& desc);
    ~TextureImpl();

    // Texture is either stored in CUDA array or mip mapped array.
    CUarray m_cudaArray = 0;
    CUmipmappedArray m_cudaMipMappedArray = 0;

    void* m_cudaExternalMemory = nullptr;

    CUDA_RESOURCE_VIEW_DESC m_baseResourceViewDesc = {};

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDefaultView(ITextureView** outTextureView) override;

public:
    struct ViewKey
    {
        Format format;
        SamplerSettings samplerSettings;
        SubresourceRange range;
        bool operator==(const ViewKey& other) const
        {
            return format == other.format && samplerSettings == other.samplerSettings && range == other.range;
        }
    };

    struct ViewKeyHasher
    {
        size_t operator()(const ViewKey& key) const
        {
            size_t hash = 0;
            hash_combine(hash, key.format);
            hash_combine(hash, key.samplerSettings.addressMode[0]);
            hash_combine(hash, key.samplerSettings.addressMode[1]);
            hash_combine(hash, key.samplerSettings.addressMode[2]);
            hash_combine(hash, key.samplerSettings.filterMode);
            hash_combine(hash, key.samplerSettings.maxAnisotropy);
            hash_combine(hash, key.samplerSettings.mipmapFilterMode);
            hash_combine(hash, key.samplerSettings.mipmapLevelBias);
            hash_combine(hash, key.samplerSettings.minMipmapLevelClamp);
            hash_combine(hash, key.samplerSettings.maxMipmapLevelClamp);
            hash_combine(hash, key.samplerSettings.borderColor[0]);
            hash_combine(hash, key.samplerSettings.borderColor[1]);
            hash_combine(hash, key.samplerSettings.borderColor[2]);
            hash_combine(hash, key.samplerSettings.borderColor[3]);
            hash_combine(hash, key.range.layer);
            hash_combine(hash, key.range.layerCount);
            hash_combine(hash, key.range.mip);
            hash_combine(hash, key.range.mipCount);
            return hash;
        }
    };

    struct SubresourceRangeHasher
    {
        size_t operator()(const SubresourceRange& key) const
        {
            size_t hash = 0;
            hash_combine(hash, key.layer);
            hash_combine(hash, key.layerCount);
            hash_combine(hash, key.mip);
            hash_combine(hash, key.mipCount);
            return hash;
        }
    };

    SamplerSettings m_defaultSamplerSettings;
    RefPtr<TextureViewImpl> m_defaultView;
    std::mutex m_mutex;
    std::unordered_map<ViewKey, CUtexObject, ViewKeyHasher> m_texObjects;
    std::unordered_map<SubresourceRange, CUsurfObject, SubresourceRangeHasher> m_surfObjects;

    CUtexObject getTexObject(Format format, const SamplerSettings& samplerSettings, const SubresourceRange& range);
    CUsurfObject getSurfObject(const SubresourceRange& range);
};

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(Device* device, const TextureViewDesc& desc);

    virtual void makeExternal() override { m_texture.establishStrongReference(); }
    virtual void makeInternal() override { m_texture.breakStrongReference(); }

    // ITextureView implementation
    virtual SLANG_NO_THROW ITexture* SLANG_MCALL getTexture() override { return m_texture; }
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(
        DescriptorHandleAccess access,
        DescriptorHandle* outHandle
    ) override;

    CUtexObject getTexObject()
    {
        if (!m_cudaTexObj)
            m_cudaTexObj =
                m_texture->getTexObject(m_desc.format, m_texture->m_defaultSamplerSettings, m_desc.subresourceRange);
        return m_cudaTexObj;
    }

    CUtexObject getTexObjectWithSamplerSettings(const SamplerSettings& samplerSettings)
    {
        return m_texture->getTexObject(m_desc.format, samplerSettings, m_desc.subresourceRange);
    }

    CUsurfObject getSurfObject()
    {
        if (!m_cudaSurfObj)
            m_cudaSurfObj = m_texture->getSurfObject(m_desc.subresourceRange);
        return m_cudaSurfObj;
    }

    BreakableReference<TextureImpl> m_texture;
    CUtexObject m_cudaTexObj = 0;
    CUsurfObject m_cudaSurfObj = 0;
};

} // namespace rhi::cuda
