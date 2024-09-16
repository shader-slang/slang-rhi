#pragma once

#include "vk-base.h"
#include "vk-device.h"

namespace rhi::vk {

struct TextureSubresourceView
{
    VkImageView imageView = VK_NULL_HANDLE;
    VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

class TextureImpl : public Texture
{
public:
    TextureImpl(RendererBase* device, const TextureDesc& desc);
    ~TextureImpl();

    VkImage m_image = VK_NULL_HANDLE;
    VkFormat m_vkformat = VK_FORMAT_R8G8B8A8_UNORM;
    VkDeviceMemory m_imageMemory = VK_NULL_HANDLE;
    bool m_isWeakImageReference = false;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;

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
            hash_combine(hash, key.range.aspectMask);
            hash_combine(hash, key.range.baseArrayLayer);
            hash_combine(hash, key.range.layerCount);
            hash_combine(hash, key.range.mipLevel);
            hash_combine(hash, key.range.mipLevelCount);
            return hash;
        }
    };

    std::unordered_map<ViewKey, TextureSubresourceView, ViewKeyHasher> m_views;

    TextureSubresourceView getView(Format format, const SubresourceRange& range);
};

} // namespace rhi::vk
