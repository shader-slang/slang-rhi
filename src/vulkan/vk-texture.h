#pragma once

#include "vk-base.h"

namespace rhi::vk {

struct TextureSubresourceView
{
    VkImageView imageView = VK_NULL_HANDLE;
    VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

class TextureImpl : public Texture
{
public:
    TextureImpl(Device* device, const TextureDesc& desc);
    ~TextureImpl();

    VkImage m_image = VK_NULL_HANDLE;
    VkFormat m_vkformat = VK_FORMAT_R8G8B8A8_UNORM;
    VkDeviceMemory m_imageMemory = VK_NULL_HANDLE;
    bool m_isWeakImageReference = false;

    bool m_isSwapchainInitialState = false;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;

    struct ViewKey
    {
        Format format;
        TextureAspect aspect;
        SubresourceRange range;
        bool operator==(const ViewKey& other) const
        {
            return format == other.format && aspect == other.aspect && range == other.range;
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

    std::mutex m_mutex;
    std::unordered_map<ViewKey, TextureSubresourceView, ViewKeyHasher> m_views;

    TextureSubresourceView getView(Format format, TextureAspect aspect, const SubresourceRange& range);
};

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(Device* device, const TextureViewDesc& desc);

    RefPtr<TextureImpl> m_texture;
    DescriptorHandle m_descriptorHandle[2] = {};

    // ITextureView implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW ITexture* SLANG_MCALL getTexture() override { return m_texture; }
    virtual SLANG_NO_THROW Result SLANG_MCALL
    getDescriptorHandle(DescriptorHandleAccess access, DescriptorHandle* outHandle) override;

    TextureSubresourceView getView();
};

} // namespace rhi::vk
