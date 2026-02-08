#pragma once

#include "vk-base.h"

namespace rhi::vk {

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
    struct View
    {
        VkImageView imageView = VK_NULL_HANDLE;
        VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    View getView(Format format, TextureAspect aspect, const SubresourceRange& range, bool isRenderTarget);

    VkImage m_image = VK_NULL_HANDLE;
    VkFormat m_vkformat = VK_FORMAT_UNDEFINED;
    VkDeviceMemory m_imageMemory = VK_NULL_HANDLE;
    // True if this texture is created from a swap chain buffer.
    // Swap chain textures are deleted immediately when deleteThis() is called.
    // Swap chain textures do not own the underlying image memory.
    bool m_isSwapchainTexture = false;
    bool m_isSwapchainInitialState = false;

    struct ViewKey
    {
        Format format;
        TextureAspect aspect;
        SubresourceRange range;
        bool isRenderTarget;
        bool operator==(const ViewKey& other) const
        {
            return format == other.format && aspect == other.aspect && range == other.range &&
                   isRenderTarget == other.isRenderTarget;
        }
    };

    struct ViewKeyHasher
    {
        size_t operator()(const ViewKey& key) const
        {
            size_t hash = 0;
            hash_combine(hash, key.format);
            hash_combine(hash, key.aspect);
            hash_combine(hash, key.range.layer);
            hash_combine(hash, key.range.layerCount);
            hash_combine(hash, key.range.mip);
            hash_combine(hash, key.range.mipCount);
            return hash;
        }
    };

    RefPtr<TextureViewImpl> m_defaultView;
    std::unordered_map<ViewKey, View, ViewKeyHasher> m_views;
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
    TextureImpl::View getView();
    TextureImpl::View getRenderTargetView();

    BreakableReference<TextureImpl> m_texture;
    /// Descriptor handles (texture read, texture write, combined texture/sampler).
    DescriptorHandle m_descriptorHandle[3] = {};
};

} // namespace rhi::vk
