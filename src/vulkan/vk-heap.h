#pragma once

#include "vk-base.h"
#include "../heap.h"

#include <mutex>

namespace rhi::vk {

class HeapImpl : public Heap
{
public:
    // Vulkan buffer alignment requirement is typically 256 bytes for UBOs,
    // but use a conservative 256 bytes to handle most buffer types
    static const Size kAlignment = 256;

    struct PendingFree
    {
        HeapAlloc allocation;
        uint64_t submitIndex;
    };

    class PageImpl : public Heap::Page
    {
    public:
        PageImpl(Heap* heap, const PageDesc& desc, VkBuffer buffer, VkDeviceMemory memory, const VulkanApi& api)
            : Heap::Page(heap, desc)
            , m_buffer(buffer)
            , m_memory(memory)
            , m_api(&api)
        {
        }

        DeviceAddress offsetToAddress(Size offset) override
        {
            if (!m_api->vkGetBufferDeviceAddress)
                return 0;

            VkBufferDeviceAddressInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            info.buffer = m_buffer;
            return (DeviceAddress)(m_api->vkGetBufferDeviceAddress(m_api->m_device, &info) + offset);
        }

        VkBuffer m_buffer;
        VkDeviceMemory m_memory;
        const VulkanApi* m_api;
    };

    HeapImpl(Device* device, const HeapDesc& desc);
    ~HeapImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL free(HeapAlloc allocation) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL flush() override;

    virtual Result allocatePage(const PageDesc& desc, Page** outPage) override;
    virtual Result freePage(Page* page) override;

    // Alignment requirements
    virtual Result fixUpAllocDesc(HeapAllocDesc& desc) override;

    std::list<PendingFree> m_pendingFrees;
};

} // namespace rhi::vk
