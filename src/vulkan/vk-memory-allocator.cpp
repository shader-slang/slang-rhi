#include "vk-memory-allocator.h"
#include "vk-device.h"
#include "vk-utils.h"

#if SLANG_WINDOWS_FAMILY
#include <dxgi1_4.h>
#endif


namespace rhi::vk {

// ----------------------------------------------------------------------------
// MemoryAllocator
// ----------------------------------------------------------------------------

namespace {

/// Translate an AllocationDesc to VMA allocation parameters.
/// VMA_MEMORY_USAGE_AUTO* requires knowing the resource, so plain memory allocations
/// (forResource == false) select the memory type through required/preferred flags only.
VmaAllocationCreateInfo getAllocationCreateInfo(const AllocationDesc& desc, bool forResource)
{
    VmaAllocationCreateInfo info = {};
    info.memoryTypeBits = desc.memoryTypeBits;
    switch (desc.memoryType)
    {
    case MemoryType::DeviceLocal:
        info.usage = forResource ? VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE : VMA_MEMORY_USAGE_UNKNOWN;
        info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
    case MemoryType::Upload:
        info.usage = forResource ? VMA_MEMORY_USAGE_AUTO_PREFER_HOST : VMA_MEMORY_USAGE_UNKNOWN;
        info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    case MemoryType::ReadBack:
        // Readback memory is read by the CPU, so prefer a cached memory type.
        // Uncached (write-combined) memory is very slow to read from the CPU.
        info.usage = forResource ? VMA_MEMORY_USAGE_AUTO_PREFER_HOST : VMA_MEMORY_USAGE_UNKNOWN;
        info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        info.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        break;
    }
    if (desc.dedicated || desc.exportable)
        info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    return info;
}

bool isOutOfMemory(VkResult result)
{
    return result == VK_ERROR_OUT_OF_DEVICE_MEMORY || result == VK_ERROR_OUT_OF_HOST_MEMORY;
}

} // namespace

Result MemoryAllocator::init(DeviceImpl* device, uint32_t vulkanApiVersion)
{
    SLANG_RHI_ASSERT(m_allocator == VK_NULL_HANDLE);

    m_device = device;
    const VulkanApi& api = device->m_api;

    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = api.vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = api.vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo createInfo = {};
    createInfo.vulkanApiVersion = vulkanApiVersion;
    createInfo.physicalDevice = api.m_physicalDevice;
    createInfo.device = api.m_device;
    createInfo.instance = api.m_instance;
    createInfo.pVulkanFunctions = &vulkanFunctions;
    if (api.m_extendedFeatures.vulkan12Features.bufferDeviceAddress)
        createInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    SLANG_VK_RETURN_ON_FAIL_REPORT(vmaCreateAllocator(&createInfo, &m_allocator), device);

    m_exportMemoryAllocateInfo.handleTypes = kSharedMemoryHandleType;
#if SLANG_WINDOWS_FAMILY
    m_exportMemoryWin32HandleInfo.dwAccess = DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE;
    m_exportMemoryAllocateInfo.pNext = &m_exportMemoryWin32HandleInfo;
#endif

    return SLANG_OK;
}

void MemoryAllocator::shutdown()
{
    if (m_allocator == VK_NULL_HANDLE)
        return;

    for (VmaPool& pool : m_exportPools)
    {
        if (pool != VK_NULL_HANDLE)
        {
            vmaDestroyPool(m_allocator, pool);
            pool = VK_NULL_HANDLE;
        }
    }
    vmaDestroyAllocator(m_allocator);
    m_allocator = VK_NULL_HANDLE;
}

bool MemoryAllocator::canExport() const
{
    // Exporting memory requires the platform specific external memory extension.
#if SLANG_WINDOWS_FAMILY
    return m_device->m_api.vkGetMemoryWin32HandleKHR != nullptr;
#else
    return m_device->m_api.vkGetMemoryFdKHR != nullptr;
#endif
}

Result MemoryAllocator::getExportPool(uint32_t memoryTypeIndex, VmaPool* outPool)
{
    if (!canExport())
        return SLANG_E_NOT_AVAILABLE;
    if (memoryTypeIndex >= VK_MAX_MEMORY_TYPES)
        return SLANG_E_INVALID_ARG;

    std::lock_guard<std::mutex> lock(m_exportPoolMutex);
    VmaPool& pool = m_exportPools[memoryTypeIndex];
    if (pool == VK_NULL_HANDLE)
    {
        VmaPoolCreateInfo poolCreateInfo = {};
        poolCreateInfo.memoryTypeIndex = memoryTypeIndex;
        poolCreateInfo.pMemoryAllocateNext = &m_exportMemoryAllocateInfo;
        SLANG_VK_RETURN_ON_FAIL_REPORT(vmaCreatePool(m_allocator, &poolCreateInfo, &pool), m_device);
    }
    *outPool = pool;
    return SLANG_OK;
}

template<typename FindMemoryType, typename Allocate>
Result MemoryAllocator::allocate(
    const AllocationDesc& desc,
    bool forResource,
    FindMemoryType findMemoryType,
    Allocate doAllocate,
    const char* what
)
{
    VmaAllocationCreateInfo allocCreateInfo = getAllocationCreateInfo(desc, forResource);

    VkResult result = VK_SUCCESS;
    if (!desc.exportable)
    {
        result = doAllocate(allocCreateInfo);
    }
    else
    {
        // Exportable memory comes from export pools, and each pool is bound to a single memory type.
        // Unlike for regular allocations, VMA can't fall back to another memory type if the
        // preferred one is out of memory, so try the compatible types in order of preference.
        result = VK_ERROR_FEATURE_NOT_PRESENT;
        uint32_t memoryTypeBits = allocCreateInfo.memoryTypeBits;
        while (memoryTypeBits != 0)
        {
            allocCreateInfo.pool = VK_NULL_HANDLE;
            allocCreateInfo.memoryTypeBits = memoryTypeBits;
            uint32_t memoryTypeIndex = 0;
            if (findMemoryType(allocCreateInfo, &memoryTypeIndex) != VK_SUCCESS)
                break;
            SLANG_RETURN_ON_FAIL(getExportPool(memoryTypeIndex, &allocCreateInfo.pool));
            result = doAllocate(allocCreateInfo);
            if (!isOutOfMemory(result))
                break;
            memoryTypeBits &= ~(1u << memoryTypeIndex);
        }
    }

    switch (result)
    {
    case VK_SUCCESS:
        return SLANG_OK;
    case VK_ERROR_FEATURE_NOT_PRESENT:
        // No memory type satisfies the requirements.
        return SLANG_E_NOT_AVAILABLE;
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return SLANG_E_OUT_OF_MEMORY;
    default:
        reportVulkanError(result, what, SLANG_RHI_SOURCE_LOCATION(), m_device);
        return SLANG_FAIL;
    }
}

Result MemoryAllocator::createBuffer(
    const VkBufferCreateInfo& createInfo_,
    const AllocationDesc& desc,
    VkDeviceSize minAlignment,
    VkBuffer* outBuffer,
    VmaAllocation* outAllocation
)
{
    VkBufferCreateInfo createInfo = createInfo_;
    VkExternalMemoryBufferCreateInfo externalCreateInfo = {VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
    if (desc.exportable)
    {
        externalCreateInfo.pNext = createInfo.pNext;
        externalCreateInfo.handleTypes = kSharedMemoryHandleType;
        createInfo.pNext = &externalCreateInfo;
    }

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    SLANG_RETURN_ON_FAIL(allocate(
        desc,
        true,
        [&](const VmaAllocationCreateInfo& info, uint32_t* outMemoryTypeIndex)
        {
            return vmaFindMemoryTypeIndexForBufferInfo(m_allocator, &createInfo, &info, outMemoryTypeIndex);
        },
        [&](const VmaAllocationCreateInfo& info)
        {
            return vmaCreateBufferWithAlignment(
                m_allocator,
                &createInfo,
                &info,
                minAlignment,
                &buffer,
                &allocation,
                nullptr
            );
        },
        "vmaCreateBufferWithAlignment"
    ));

    *outBuffer = buffer;
    *outAllocation = allocation;
    return SLANG_OK;
}

Result MemoryAllocator::createImage(
    const VkImageCreateInfo& createInfo_,
    const AllocationDesc& desc,
    VkImage* outImage,
    VmaAllocation* outAllocation
)
{
    VkImageCreateInfo createInfo = createInfo_;
    VkExternalMemoryImageCreateInfo externalCreateInfo = {VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    if (desc.exportable)
    {
        externalCreateInfo.pNext = createInfo.pNext;
        externalCreateInfo.handleTypes = kSharedMemoryHandleType;
        createInfo.pNext = &externalCreateInfo;
    }

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    SLANG_RETURN_ON_FAIL(allocate(
        desc,
        true,
        [&](const VmaAllocationCreateInfo& info, uint32_t* outMemoryTypeIndex)
        {
            return vmaFindMemoryTypeIndexForImageInfo(m_allocator, &createInfo, &info, outMemoryTypeIndex);
        },
        [&](const VmaAllocationCreateInfo& info)
        {
            return vmaCreateImage(m_allocator, &createInfo, &info, &image, &allocation, nullptr);
        },
        "vmaCreateImage"
    ));

    *outImage = image;
    *outAllocation = allocation;
    return SLANG_OK;
}

Result MemoryAllocator::allocateMemory(
    const VkMemoryRequirements& requirements,
    const AllocationDesc& desc,
    VmaAllocation* outAllocation
)
{
    if (desc.exportable)
        return SLANG_E_NOT_IMPLEMENTED;

    VmaAllocation allocation = VK_NULL_HANDLE;
    SLANG_RETURN_ON_FAIL(allocate(
        desc,
        false,
        [](const VmaAllocationCreateInfo&, uint32_t*)
        {
            return VK_ERROR_FEATURE_NOT_PRESENT;
        },
        [&](const VmaAllocationCreateInfo& info)
        {
            return vmaAllocateMemory(m_allocator, &requirements, &info, &allocation, nullptr);
        },
        "vmaAllocateMemory"
    ));

    *outAllocation = allocation;
    return SLANG_OK;
}

void MemoryAllocator::destroyBuffer(VkBuffer buffer, VmaAllocation allocation)
{
    vmaDestroyBuffer(m_allocator, buffer, allocation);
}

void MemoryAllocator::destroyImage(VkImage image, VmaAllocation allocation)
{
    vmaDestroyImage(m_allocator, image, allocation);
}

void MemoryAllocator::freeMemory(VmaAllocation allocation)
{
    vmaFreeMemory(m_allocator, allocation);
}

VmaAllocationInfo MemoryAllocator::getAllocationInfo(VmaAllocation allocation) const
{
    VmaAllocationInfo info = {};
    if (allocation != VK_NULL_HANDLE)
        vmaGetAllocationInfo(m_allocator, allocation, &info);
    return info;
}

Result MemoryAllocator::exportHandle(VmaAllocation allocation, NativeHandle* outHandle)
{
    SLANG_RHI_ASSERT(allocation != VK_NULL_HANDLE);
    const VkDeviceMemory memory = getAllocationInfo(allocation).deviceMemory;
    const VulkanApi& api = m_device->m_api;
#if SLANG_WINDOWS_FAMILY
    if (!api.vkGetMemoryWin32HandleKHR)
        return SLANG_E_NOT_AVAILABLE;
    VkMemoryGetWin32HandleInfoKHR info = {VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR};
    info.memory = memory;
    info.handleType = kSharedMemoryHandleType;
    HANDLE handle = NULL;
    SLANG_VK_RETURN_ON_FAIL_REPORT(api.vkGetMemoryWin32HandleKHR(api.m_device, &info, &handle), m_device);
    *outHandle = NativeHandle{NativeHandleType::Win32, (uint64_t)handle};
#else
    if (!api.vkGetMemoryFdKHR)
        return SLANG_E_NOT_AVAILABLE;
    VkMemoryGetFdInfoKHR info = {VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
    info.memory = memory;
    info.handleType = kSharedMemoryHandleType;
    int handle = -1;
    SLANG_VK_RETURN_ON_FAIL_REPORT(api.vkGetMemoryFdKHR(api.m_device, &info, &handle), m_device);
    *outHandle = NativeHandle{NativeHandleType::FileDescriptor, (uint64_t)handle};
#endif
    return SLANG_OK;
}

} // namespace rhi::vk
