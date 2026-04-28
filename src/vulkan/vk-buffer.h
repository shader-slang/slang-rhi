#pragma once

#include "vk-base.h"

namespace rhi::vk {

// Combined buffer and memory class
class VKBufferHandleRAII
{
public:
    /// Initialize a buffer with specified size and external memory support
    Result init(
        const VulkanApi& api,
        Size bufferSize,
        VkBufferUsageFlags usage,
        MemoryType memoryType,
        VkExternalMemoryHandleTypeFlagsKHR externalMemoryHandleTypeFlags = 0
    );

    /// Initialize a buffer using VMA for memory allocation
    Result init(
        const VulkanApi& api,
        VmaAllocator vmaAllocator,
        Size bufferSize,
        VkBufferUsageFlags usage,
        MemoryType memoryType
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
            if (m_vmaAllocation)
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
            else
            {
                m_api->vkDestroyBuffer(m_api->m_device, m_buffer, nullptr);
                m_api->vkFreeMemory(m_api->m_device, m_memory, nullptr);
            }
        }
    }

    VkBuffer m_buffer;
    VkDeviceMemory m_memory;
    const VulkanApi* m_api;
    VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;
    VmaAllocation m_vmaAllocation = VK_NULL_HANDLE;
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
    VKBufferHandleRAII m_uploadBuffer;
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
