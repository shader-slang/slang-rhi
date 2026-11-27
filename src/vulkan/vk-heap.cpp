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
}

Result HeapImpl::PageImpl::init()
{
    const VulkanApi& api = m_device->m_api;

    // Determine buffer usage flags for heap pages
    VkBufferUsageFlags usage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    // Enable device address if supported
    if (api.m_extendedFeatures.vulkan12Features.bufferDeviceAddress)
    {
        usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }

    // Allocate the page through VMA. The page may be sub-allocated from a larger memory block,
    // so only access its memory through the buffer (shared heaps get dedicated, exportable memory).
    // Allocations within the page are aligned relative to its start, so the page itself must be
    // aligned to the requested alignment.
    HeapImpl* heapImpl = static_cast<HeapImpl*>(m_heap);
    AllocationDesc allocationDesc;
    allocationDesc.memoryType = heapImpl->m_desc.memoryType;
    allocationDesc.exportable = is_set(heapImpl->m_desc.usage, HeapUsage::Shared);
    return allocateVkBuffer(m_device, m_desc.size, usage, allocationDesc, &m_buffer, &m_allocation, m_desc.alignment);
}

HeapImpl::PageImpl::~PageImpl()
{
    m_device->m_memoryAllocator.destroyBuffer(m_buffer, m_allocation);
}

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

    // Create the page and allocate its buffer. Allocation can fail, e.g. when out of memory or
    // when a shared heap is requested but the device can't export memory.
    PageImpl* page = new PageImpl(this, desc, deviceImpl);
    Result result = page->init();
    if (SLANG_FAILED(result))
    {
        delete page;
        return result;
    }

    // The page is allocated with the requested alignment (see PageImpl::init), so this only
    // guards against an allocator that doesn't honor it.
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

    // The page destructor releases the buffer and its memory.
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
