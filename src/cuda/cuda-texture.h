#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

bool isFormatSupported(Format format);

class TextureImpl : public Texture
{
public:
    TextureImpl(Device* device, const TextureDesc& desc);
    ~TextureImpl();

    // Texture is either stored in cuda array or mip mapped array.
    CUarray m_cudaArray = 0;
    CUmipmappedArray m_cudaMipMappedArray = 0;

    void* m_cudaExternalMemory = nullptr;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

public:
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

    std::mutex m_mutex;
    std::unordered_map<ViewKey, CUtexObject, ViewKeyHasher> m_texObjects;
    std::unordered_map<SubresourceRange, CUsurfObject, SubresourceRangeHasher> m_surfObjects;

    CUtexObject getTexObject(Format format, const SubresourceRange& range);
    CUsurfObject getSurfObject(const SubresourceRange& range);
};

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(Device* device, const TextureViewDesc& desc);

    // ITextureView implementation
    virtual SLANG_NO_THROW ITexture* SLANG_MCALL getTexture() override { return m_texture; }

    CUtexObject getTexObject()
    {
        if (!m_cudaTexObj)
            m_cudaTexObj = m_texture->getTexObject(m_desc.format, m_desc.subresourceRange);
        return m_cudaTexObj;
    }

    CUsurfObject getSurfObject()
    {
        if (!m_cudaSurfObj)
            m_cudaSurfObj = m_texture->getSurfObject(m_desc.subresourceRange);
        return m_cudaSurfObj;
    }

    RefPtr<TextureImpl> m_texture;
    CUtexObject m_cudaTexObj = 0;
    CUsurfObject m_cudaSurfObj = 0;
};

} // namespace rhi::cuda
