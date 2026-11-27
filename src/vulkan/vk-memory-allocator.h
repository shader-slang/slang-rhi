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
    /// Exportable allocations are always dedicated allocations (allocated with
    /// VkMemoryDedicatedAllocateInfo), which importers such as CUDA rely on.
    bool canExport() const;

    // Allocations are VmaAllocation handles. Unless allocated as dedicated, an allocation is a
    // sub-range of a VkDeviceMemory block that is shared with other allocations, so it must always
    // be addressed with VmaAllocationInfo::offset (see getAllocationInfo()).

    /// Create a buffer and bind it to newly allocated memory.
    /// The buffer's address is aligned to at least minAlignment.
    /// Destroy the buffer and free the memory with destroyBuffer().
    Result createBuffer(
        const VkBufferCreateInfo& createInfo,
        const AllocationDesc& desc,
        VkDeviceSize minAlignment,
        VkBuffer* outBuffer,
        VmaAllocation* outAllocation
    );

    /// Create an image and bind it to newly allocated memory.
    /// Destroy the image and free the memory with destroyImage().
    Result createImage(
        const VkImageCreateInfo& createInfo,
        const AllocationDesc& desc,
        VkImage* outImage,
        VmaAllocation* outAllocation
    );

    /// Allocate memory that is not tied to a resource. Exportable memory is not supported.
    /// Free the memory with freeMemory(), after destroying all resources bound to it.
    Result allocateMemory(
        const VkMemoryRequirements& requirements,
        const AllocationDesc& desc,
        VmaAllocation* outAllocation
    );

    /// Destroy a buffer, then free its memory. Either handle may be null.
    void destroyBuffer(VkBuffer buffer, VmaAllocation allocation);

    /// Destroy an image, then free its memory. Either handle may be null.
    void destroyImage(VkImage image, VmaAllocation allocation);

    /// Free memory. The allocation may be null.
    void freeMemory(VmaAllocation allocation);

    /// Query the allocation's current memory, offset, memory type and mapped pointer.
    /// pMappedData is the persistently mapped pointer to the start of the allocation, and is
    /// null unless the allocation was created for Upload or ReadBack memory.
    /// Returns zeroed info for a null allocation.
    VmaAllocationInfo getAllocationInfo(VmaAllocation allocation) const;

    /// Export an OS handle (Win32 handle or file descriptor) to an allocation's memory.
    /// The allocation must have been created as exportable (AllocationDesc::exportable).
    /// The caller owns the returned handle.
    Result exportHandle(VmaAllocation allocation, NativeHandle* outHandle);

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
    /// VMA only allocates dedicated memory with VkMemoryDedicatedAllocateInfo on Vulkan 1.1+.
    bool m_supportsDedicatedAllocation = false;

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
