#pragma once

#include "vk-base.h"

namespace rhi::vk {

// Prefer the requested properties when available, falling back to a required-only memory
// type if the preferred heap is exhausted. Optionally return the type actually allocated.
Result allocateVkMemory(
    const VulkanApi& api,
    const VkMemoryRequirements& memoryReqs,
    VkMemoryPropertyFlags reqMemoryProperties,
    VkMemoryPropertyFlags preferredMemoryProperties,
    bool needsDeviceAddress,
    VkExternalMemoryHandleTypeFlagsKHR externalMemoryHandleTypeFlags,
    VkDeviceMemory* outMemory,
    uint32_t* outMemoryTypeIndex = nullptr
);

Result createVkBuffer(
    const VulkanApi& api,
    Size bufferSize,
    VkBufferUsageFlags usage,
    VkExternalMemoryHandleTypeFlagsKHR externalMemoryHandleTypeFlags,
    VkBuffer* outBuffer
);

// Combined buffer and memory class
class VKBufferHandleRAII
{
public:
    /// Initialize a buffer with specified size, and memory props.
    /// The memory type must have all of reqMemoryProperties, and one that also has
    /// preferredMemoryProperties is used if available.
    Result init(
        const VulkanApi& api,
        Size bufferSize,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags reqMemoryProperties,
        VkExternalMemoryHandleTypeFlagsKHR externalMemoryHandleTypeFlags = 0,
        VkMemoryPropertyFlags preferredMemoryProperties = 0
    );

    /// Create a buffer and bind it to existing memory.
    Result initPlaced(
        const VulkanApi& api,
        Size bufferSize,
        VkBufferUsageFlags usage,
        uint32_t memoryTypeIndex,
        VkDeviceMemory memory,
        Offset offset
    );

    /// Wrap a buffer without taking ownership of the buffer or its memory.
    void initFromNativeHandle(const VulkanApi& api, VkBuffer buffer);

    /// Returns true if has been initialized
    bool isInitialized() const { return m_api != nullptr; }

    VKBufferHandleRAII() = default;
    VKBufferHandleRAII(const VKBufferHandleRAII&) = delete;
    VKBufferHandleRAII& operator=(const VKBufferHandleRAII&) = delete;

    ~VKBufferHandleRAII()
    {
        if (m_ownsBuffer && m_buffer != VK_NULL_HANDLE)
            m_api->vkDestroyBuffer(m_api->m_device, m_buffer, nullptr);
        if (m_ownsMemory && m_memory != VK_NULL_HANDLE)
            m_api->vkFreeMemory(m_api->m_device, m_memory, nullptr);
    }

    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    const VulkanApi* m_api = nullptr;
    bool m_ownsBuffer = false;
    bool m_ownsMemory = false;
};

class BufferImpl : public Buffer
{
public:
    BufferImpl(Device* device, const BufferDesc& desc);
    ~BufferImpl();

    virtual void deleteThis() override;

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // IBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(
        DescriptorHandleAccess access,
        Format format,
        BufferRange range,
        DescriptorHandle* outHandle
    ) override;

public:
    VKBufferHandleRAII m_buffer;
    DeviceAddress m_deviceAddress = 0;

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
