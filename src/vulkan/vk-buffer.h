#pragma once

#include "vk-base.h"

namespace rhi::vk {

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
    /// Initialize a buffer with memory allocated through VMA.
    /// If exportable is true, the buffer gets its own VkDeviceMemory that can be
    /// exported via OS handles (for shared buffers).
    Result init(
        DeviceImpl* device,
        Size bufferSize,
        VkBufferUsageFlags usage,
        MemoryType memoryType,
        bool exportable = false
    );

    /// Create a buffer and bind it to existing memory (not owned, not managed by VMA).
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
        if (m_vmaAllocation != VK_NULL_HANDLE)
        {
            // Safety net: callers (e.g. DeviceImpl::mapBuffer/unmapBuffer) must
            // perform balanced vmaMapMemory/vmaUnmapMemory calls. This handles
            // at most one leaked map (e.g. from an early-return error path).
            // VMA tracks a per-allocation map count; only a single outstanding
            // map is expected here - multiple outstanding maps indicate a bug.
            VmaAllocationInfo allocInfo;
            vmaGetAllocationInfo(m_vmaAllocator, m_vmaAllocation, &allocInfo);
            if (allocInfo.pMappedData)
            {
                SLANG_RHI_ASSERT_FAILURE("VMA allocation destroyed while still mapped");
                vmaUnmapMemory(m_vmaAllocator, m_vmaAllocation);
            }
            vmaDestroyBuffer(m_vmaAllocator, m_buffer, m_vmaAllocation);
        }
        else if (m_ownsBuffer && m_buffer != VK_NULL_HANDLE)
        {
            m_api->vkDestroyBuffer(m_api->m_device, m_buffer, nullptr);
        }
    }

    VkBuffer m_buffer = VK_NULL_HANDLE;
    /// Backing device memory. For VMA allocations this may be a block shared with
    /// other allocations; for placed buffers it is owned by the resource heap.
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    const VulkanApi* m_api = nullptr;
    VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;
    VmaAllocation m_vmaAllocation = VK_NULL_HANDLE;
    /// True if m_buffer was created by initPlaced() and must be destroyed with vkDestroyBuffer.
    bool m_ownsBuffer = false;
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
