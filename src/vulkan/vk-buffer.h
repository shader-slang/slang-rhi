#pragma once

#include "vk-base.h"
#include "vk-memory-allocator.h"

namespace rhi::vk {

/// Create a buffer without binding memory to it.
Result createVkBuffer(const VulkanApi& api, Size size, VkBufferUsageFlags usage, VkBuffer* outBuffer);

/// Create a buffer and bind it to newly allocated memory.
/// Buffers with a device address are aligned for all their uses (see getBufferDeviceAddressAlignment),
/// and to at least minAlignment.
Result allocateVkBuffer(
    DeviceImpl* device,
    Size size,
    VkBufferUsageFlags usage,
    const AllocationDesc& allocationDesc,
    VkBuffer* outBuffer,
    VmaAllocation* outAllocation,
    VkDeviceSize minAlignment = 1
);

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

    /// Returns a pointer to the buffer's host visible memory, or nullptr if it is not host visible.
    void* getMappedData() const;

public:
    VkBuffer m_buffer = VK_NULL_HANDLE;
    /// False for buffers created from native handles.
    bool m_ownsBuffer = true;
    /// Memory backing the buffer. Empty for placed buffers (bound to the resource heap's memory)
    /// and for buffers created from native handles.
    VmaAllocation m_allocation = VK_NULL_HANDLE;
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
