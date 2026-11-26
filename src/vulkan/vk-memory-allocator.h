#pragma once

#include "vk-base.h"

#include <mutex>

namespace rhi::vk {

/// Handle type used for exporting the memory of shared buffers, textures and heaps.
#if SLANG_WINDOWS_FAMILY
inline constexpr VkExternalMemoryHandleTypeFlagBits kSharedMemoryHandleType =
    VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
inline constexpr VkExternalMemoryHandleTypeFlagBits kSharedMemoryHandleType =
    VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif

/// Describes how device memory is allocated.
struct AllocationDesc
{
    /// DeviceLocal memory is not host visible. Upload and ReadBack memory is host visible and
    /// coherent, and is persistently mapped (ReadBack prefers host cached memory).
    MemoryType memoryType = MemoryType::DeviceLocal;
    /// Allocate a dedicated VkDeviceMemory instead of sub-allocating from a larger block.
    bool dedicated = false;
    /// Allocate dedicated memory that can be exported through an OS handle (implies dedicated).
    /// The resource must be created with kSharedMemoryHandleType, which MemoryAllocator handles.
    bool exportable = false;
    /// Restricts the allocation to a subset of memory types.
    uint32_t memoryTypeBits = UINT32_MAX;
};

/// Owning handle to device memory allocated through MemoryAllocator.
/// Unless allocated as dedicated, the memory is a sub-range of a VkDeviceMemory block that is
/// shared with other allocations, so it must always be addressed with getOffset().
/// Any resource bound to the memory must be destroyed before the allocation.
class MemoryAllocation
{
public:
    MemoryAllocation() = default;
    ~MemoryAllocation() { reset(); }

    MemoryAllocation(const MemoryAllocation&) = delete;
    MemoryAllocation& operator=(const MemoryAllocation&) = delete;
    MemoryAllocation(MemoryAllocation&& other) noexcept { *this = std::move(other); }
    MemoryAllocation& operator=(MemoryAllocation&& other) noexcept;

    explicit operator bool() const { return m_allocation != VK_NULL_HANDLE; }

    VkDeviceMemory getDeviceMemory() const { return m_deviceMemory; }
    VkDeviceSize getOffset() const { return m_offset; }
    uint32_t getMemoryTypeIndex() const { return m_memoryTypeIndex; }
    /// Persistently mapped pointer to the start of the allocation.
    /// Null unless the allocation was created for Upload or ReadBack memory.
    void* getMappedData() const { return m_mappedData; }
    /// True if the allocation is dedicated memory that can be exported (see MemoryAllocator::exportHandle).
    bool isExportable() const { return m_exportable; }

    /// Free the memory.
    void reset();

private:
    void assign(VmaAllocator allocator, VmaAllocation allocation, const VmaAllocationInfo& info, bool exportable);

    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkDeviceMemory m_deviceMemory = VK_NULL_HANDLE;
    VkDeviceSize m_offset = 0;
    uint32_t m_memoryTypeIndex = 0;
    void* m_mappedData = nullptr;
    bool m_exportable = false;

    friend class MemoryAllocator;
};

/// Allocates all device memory of a device (buffers, textures, heaps and resource heaps)
/// through Vulkan Memory Allocator (VMA).
class MemoryAllocator
{
public:
    MemoryAllocator() = default;
    ~MemoryAllocator() { shutdown(); }

    MemoryAllocator(const MemoryAllocator&) = delete;
    MemoryAllocator& operator=(const MemoryAllocator&) = delete;

    /// vulkanApiVersion must not exceed the version the instance and the device support,
    /// as VMA uses core functions up to that version.
    Result init(DeviceImpl* device, uint32_t vulkanApiVersion);

    /// Destroy the allocator. All allocations must have been freed.
    void shutdown();

    /// True if exportable allocations are supported.
    bool canExport() const;

    /// Create a buffer and bind it to newly allocated memory.
    /// The buffer's address is aligned to at least minAlignment.
    Result createBuffer(
        const VkBufferCreateInfo& createInfo,
        const AllocationDesc& desc,
        VkDeviceSize minAlignment,
        VkBuffer* outBuffer,
        MemoryAllocation& outAllocation
    );

    /// Create an image and bind it to newly allocated memory.
    Result createImage(
        const VkImageCreateInfo& createInfo,
        const AllocationDesc& desc,
        VkImage* outImage,
        MemoryAllocation& outAllocation
    );

    /// Allocate memory that is not tied to a resource. Exportable memory is not supported.
    Result allocateMemory(
        const VkMemoryRequirements& requirements,
        const AllocationDesc& desc,
        MemoryAllocation& outAllocation
    );

    /// Export an OS handle (Win32 handle or file descriptor) to an exportable allocation's memory.
    /// The caller owns the returned handle.
    Result exportHandle(const MemoryAllocation& allocation, NativeHandle* outHandle);

private:
    /// Returns the VMA pool used for exportable allocations of the given memory type.
    /// Pools are created on demand, and their memory is allocated with VkExportMemoryAllocateInfo.
    Result getExportPool(uint32_t memoryTypeIndex, VmaPool* outPool);

    /// Allocate memory with doAllocate(), choosing an export pool for exportable allocations.
    template<typename FindMemoryType, typename Allocate>
    Result allocate(
        const AllocationDesc& desc,
        bool forResource,
        FindMemoryType findMemoryType,
        Allocate doAllocate,
        const char* what
    );

    DeviceImpl* m_device = nullptr;
    VmaAllocator m_allocator = VK_NULL_HANDLE;

    std::mutex m_exportPoolMutex;
    VmaPool m_exportPools[VK_MAX_MEMORY_TYPES] = {};
    // pNext chain of the export pools' memory allocations (must outlive the pools).
    VkExportMemoryAllocateInfo m_exportMemoryAllocateInfo = {VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
#if SLANG_WINDOWS_FAMILY
    VkExportMemoryWin32HandleInfoKHR m_exportMemoryWin32HandleInfo = {
        VK_STRUCTURE_TYPE_EXPORT_MEMORY_WIN32_HANDLE_INFO_KHR
    };
#endif
};

} // namespace rhi::vk
