#pragma once

#include "vk-base.h"

namespace rhi::vk {

class VKBufferHandleRAII
{
public:
    /// Initialize a buffer with specified size, and memory props
    Result init(
        const VulkanApi& api,
        Size bufferSize,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags reqMemoryProperties,
        VkExternalMemoryHandleTypeFlagsKHR externalMemoryHandleTypeFlags = 0
    );

    /// Returns true if has been initialized
    bool isInitialized() const { return m_api != nullptr; }

    VKBufferHandleRAII()
        : m_api(nullptr)
    {
    }

    ~VKBufferHandleRAII()
    {
        if (m_api)
        {
            m_api->vkDestroyBuffer(m_api->m_device, m_buffer, nullptr);
            m_api->vkFreeMemory(m_api->m_device, m_memory, nullptr);
        }
    }

    VkBuffer m_buffer;
    VkDeviceMemory m_memory;
    const VulkanApi* m_api;
};

class BufferImpl : public Buffer
{
public:
    BufferImpl(Device* device, const BufferDesc& desc);
    ~BufferImpl();

    VKBufferHandleRAII m_buffer;
    VKBufferHandleRAII m_uploadBuffer;

    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(
        DescriptorHandleAccess access,
        Format format,
        BufferRange range,
        DescriptorHandle* outHandle
    ) override;

    struct ViewKey
    {
        Format format;
        BufferRange range;
        bool operator==(const ViewKey& other) const { return format == other.format && range == other.range; }
    };

    struct ViewKeyHasher
    {
        size_t operator()(const ViewKey& key) const
        {
            size_t hash = 0;
            hash_combine(hash, key.format);
            hash_combine(hash, key.range.offset);
            hash_combine(hash, key.range.size);
            return hash;
        }
    };

    std::mutex m_mutex;
    std::unordered_map<ViewKey, VkBufferView, ViewKeyHasher> m_views;

    VkBufferView getView(Format format, const BufferRange& range);

    struct DescriptorHandleKey
    {
        DescriptorHandleAccess access;
        Format format;
        BufferRange range;
        bool operator==(const DescriptorHandleKey& other) const
        {
            return access == other.access && format == other.format && range == other.range;
        }
    };

    struct DescriptorHandleKeyHasher
    {
        size_t operator()(const DescriptorHandleKey& key) const
        {
            size_t hash = 0;
            hash_combine(hash, key.access);
            hash_combine(hash, key.format);
            hash_combine(hash, key.range.offset);
            hash_combine(hash, key.range.size);
            return hash;
        }
    };

    std::unordered_map<DescriptorHandleKey, DescriptorHandle, DescriptorHandleKeyHasher> m_descriptorHandles;
};

} // namespace rhi::vk
