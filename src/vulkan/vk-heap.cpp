#include "vk-heap.h"
#include "vk-device.h"
#include "vk-utils.h"
#include "vk-command.h"
#include "core/common.h"

namespace rhi::vk {

HeapImpl::PageImpl::PageImpl(Heap* heap, const PageDesc& desc, DeviceImpl* device)
    : Heap::Page(heap, desc)
    , m_device(device)
{
    // Initialize buffer similar to BufferImpl::createBuffer logic
    const VulkanApi& api = device->m_api;

    // Determine buffer usage flags for heap pages
    VkBufferUsageFlags usage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    // Enable device address if supported
    if (api.m_extendedFeatures.vulkan12Features.bufferDeviceAddress)
    {
        usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }

    // Determine memory properties based on heap memory type
    VkMemoryPropertyFlags reqMemoryProperties;
    HeapImpl* heapImpl = static_cast<HeapImpl*>(heap);

    switch (heapImpl->m_desc.memoryType)
    {
    case MemoryType::DeviceLocal:
        reqMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
    case MemoryType::Upload:
        reqMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    case MemoryType::ReadBack:
        reqMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                              VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        break;
    default:
        reqMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
    }

    // Check if external memory (shared) is needed
    VkExternalMemoryHandleTypeFlagsKHR externalMemoryHandleTypeFlags = 0;
    if (is_set(heapImpl->m_desc.usage, HeapUsage::Shared))
    {
#if SLANG_WINDOWS_FAMILY
        externalMemoryHandleTypeFlags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
        externalMemoryHandleTypeFlags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
    }

    // Initialize the buffer using the existing logic
    Result result = m_buffer.init(api, desc.size, usage, reqMemoryProperties, externalMemoryHandleTypeFlags);
    SLANG_RHI_ASSERT(SLANG_SUCCEEDED(result));
}

HeapImpl::PageImpl::~PageImpl() {}

DeviceAddress HeapImpl::PageImpl::offsetToAddress(Size offset)
{
    if (!m_device->m_api.vkGetBufferDeviceAddress)
        return 0;

    VkBufferDeviceAddressInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = m_buffer.m_buffer;
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
    // We don't call updateLastFinishedID for every free, as it's pretty costly
    if (queue->m_lastFinishedID >= queue->m_lastSubmittedID)
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

    // Create page - the constructor will handle all buffer creation logic
    Page* page = new PageImpl(this, desc, deviceImpl);

    // Vulkan memory allocation guarantees an alignment suitable for
    // any memory type, however if the user asks for an alignment
    // higher than that, the page may end up misaligned. For now,
    // treat this as an error, as the user should only ever be asking
    // for alignments based on correct memory requirements.
    if (page->offsetToAddress(0) % desc.alignment != 0)
    {
        delete page;
        return SLANG_E_INVALID_ARG;
    }
    *outPage = page;

    return SLANG_OK;
}

Result HeapImpl::freePage(Page* page_)
{
    PageImpl* page = static_cast<PageImpl*>(page_);

    // VKBufferHandleRAII destructor will automatically clean up buffer and memory
    delete page;
    return SLANG_OK;
}

Result HeapImpl::fixUpAllocDesc(HeapAllocDesc& desc)
{
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
