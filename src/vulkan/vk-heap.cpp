#include "vk-heap.h"
#include "vk-device.h"
#include "vk-utils.h"
#include "vk-command.h"

namespace rhi::vk {

HeapImpl::PageImpl::PageImpl(
    Heap* heap,
    const PageDesc& desc,
    VkBuffer buffer,
    VkDeviceMemory memory,
    DeviceImpl* device
)
    : Heap::Page(heap, desc)
    , m_buffer(buffer)
    , m_memory(memory)
    , m_device(device)
{
}

HeapImpl::PageImpl::~PageImpl() {}

DeviceAddress HeapImpl::PageImpl::offsetToAddress(Size offset)
{
    if (!m_device->m_api.vkGetBufferDeviceAddress)
        return 0;

    VkBufferDeviceAddressInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = m_buffer;
    return (DeviceAddress)(m_device->m_api.vkGetBufferDeviceAddress(m_device->m_api.m_device, &info) + offset);
}

HeapImpl::HeapImpl(Device* device, const HeapDesc& desc)
    : Heap(device, desc)
{
}

HeapImpl::~HeapImpl() {}

Result HeapImpl::free(HeapAlloc allocation)
{
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    CommandQueueImpl* queue = deviceImpl->m_queue;

    // Check if the queue has finished all work by comparing submission IDs
    if (queue->updateLastFinishedID() >= queue->m_lastSubmittedID)
    {
        // Queue is idle, we can immediately retire the allocation
        return retire(allocation);
    }
    else
    {
        // Queue is busy, add to pending frees list
        PendingFree pendingFree;
        pendingFree.allocation = allocation;
        pendingFree.submitIndex = queue->m_lastSubmittedID;
        m_pendingFrees.push_back(pendingFree);
        return SLANG_OK;
    }
}

Result HeapImpl::flush()
{
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    CommandQueueImpl* queue = deviceImpl->m_queue;

    // Update the last finished ID to get current GPU completion status
    uint64_t lastFinishedID = queue->updateLastFinishedID();

    // Process pending frees in order
    for (auto it = m_pendingFrees.begin(); it != m_pendingFrees.end();)
    {
        if (it->submitIndex <= lastFinishedID)
        {
            // This submission has completed, we can safely retire the allocation
            SLANG_RETURN_ON_FAIL(retire(it->allocation));
            it = m_pendingFrees.erase(it);
        }
        else
        {
            // List is ordered by submission, so we can break early
            // when we hit the first unfinished submission
            break;
        }
    }

    return SLANG_OK;
}

Result HeapImpl::allocatePage(const PageDesc& desc, Page** outPage)
{
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    const VulkanApi& api = deviceImpl->m_api;

    // Create buffer for device address support
    VkBufferCreateInfo bufferCreateInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferCreateInfo.size = desc.size;
    bufferCreateInfo.usage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Enable device address if supported
    if (api.m_extendedFeatures.vulkan12Features.bufferDeviceAddress)
    {
        bufferCreateInfo.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }

    VkBuffer buffer;
    VkResult result = api.vkCreateBuffer(api.m_device, &bufferCreateInfo, nullptr, &buffer);
    if (result != VK_SUCCESS)
    {
        return SLANG_FAIL;
    }

    // Get memory requirements
    VkMemoryRequirements memoryReqs;
    api.vkGetBufferMemoryRequirements(api.m_device, buffer, &memoryReqs);

    // Determine memory properties based on heap memory type
    VkMemoryPropertyFlags reqMemoryProperties;
    if (m_desc.memoryType == MemoryType::DeviceLocal)
    {
        reqMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
    else if (m_desc.memoryType == MemoryType::Upload)
    {
        reqMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    else if (m_desc.memoryType == MemoryType::ReadBack)
    {
        reqMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                              VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }
    else
    {
        reqMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    // Find suitable memory type using actual memory type bits from buffer requirements
    int memoryTypeIndex = api.findMemoryTypeIndex(memoryReqs.memoryTypeBits, reqMemoryProperties);
    if (memoryTypeIndex < 0)
    {
        api.vkDestroyBuffer(api.m_device, buffer, nullptr);
        return SLANG_FAIL;
    }

    // Allocate memory
    VkMemoryAllocateInfo allocateInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.allocationSize = memoryReqs.size;
    allocateInfo.memoryTypeIndex = memoryTypeIndex;

    // Enable device address allocation if needed and supported
    VkMemoryAllocateFlagsInfo flagInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    if (api.m_extendedFeatures.vulkan12Features.bufferDeviceAddress &&
        (bufferCreateInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT))
    {
        flagInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        allocateInfo.pNext = &flagInfo;
    }

    VkDeviceMemory memory;
    result = api.vkAllocateMemory(api.m_device, &allocateInfo, nullptr, &memory);
    if (result != VK_SUCCESS)
    {
        api.vkDestroyBuffer(api.m_device, buffer, nullptr);
        return SLANG_FAIL;
    }

    // Bind buffer to memory
    result = api.vkBindBufferMemory(api.m_device, buffer, memory, 0);
    if (result != VK_SUCCESS)
    {
        api.vkFreeMemory(api.m_device, memory, nullptr);
        api.vkDestroyBuffer(api.m_device, buffer, nullptr);
        return SLANG_FAIL;
    }

    // Create page with buffer and memory
    *outPage = new PageImpl(this, desc, buffer, memory, deviceImpl);

    return SLANG_OK;
}

Result HeapImpl::freePage(Page* page_)
{
    DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(getDevice());
    const VulkanApi& api = deviceImpl->m_api;

    PageImpl* page = static_cast<PageImpl*>(page_);

    // Clean up Vulkan buffer and memory
    if (page->m_buffer != VK_NULL_HANDLE)
    {
        api.vkDestroyBuffer(api.m_device, page->m_buffer, nullptr);
    }
    if (page->m_memory != VK_NULL_HANDLE)
    {
        api.vkFreeMemory(api.m_device, page->m_memory, nullptr);
    }

    delete page;
    return SLANG_OK;
}

Result HeapImpl::fixUpAllocDesc(HeapAllocDesc& desc)
{
    // Vulkan has various alignment requirements:
    // - Storage buffers: typically 16 bytes minimum
    // - Uniform buffers: up to 256 bytes on some hardware
    // - Device address: usually requires natural alignment

    // Use a conservative alignment that works for most buffer types
    if (desc.alignment < kAlignment)
    {
        desc.alignment = kAlignment;
    }

    // Ensure alignment is power of 2
    if (!math::isPowerOf2(desc.alignment))
    {
        return SLANG_E_INVALID_ARG;
    }

    return SLANG_OK;
}

Result DeviceImpl::createHeap(const HeapDesc& desc, IHeap** outHeap)
{
    RefPtr<HeapImpl> heap = new HeapImpl(this, desc);
    returnComPtr(outHeap, heap);
    return SLANG_OK;
}

} // namespace rhi::vk
