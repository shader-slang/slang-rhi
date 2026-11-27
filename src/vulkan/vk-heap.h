#pragma once

#include "vk-base.h"
#include "vk-buffer.h"
#include "../heap.h"

#include <mutex>

namespace rhi::vk {

class HeapImpl : public Heap
{
public:
    struct PendingFree
    {
        HeapAlloc allocation;
        uint64_t submitIndex;
    };

    class PageImpl : public Heap::Page
    {
    public:
        PageImpl(Heap* heap, const PageDesc& desc, DeviceImpl* device);
        ~PageImpl();

        /// Allocate the page's buffer. Must be called (and succeed) before the page is used.
        Result init();

        DeviceAddress offsetToAddress(Size offset) override;

        DeviceImpl* m_device;
        VkBuffer m_buffer = VK_NULL_HANDLE;
        VmaAllocation m_allocation = VK_NULL_HANDLE;
        /// Device address of the page's start (0 if buffer device addresses are not supported).
        /// The page may start at an offset within the buffer, see init().
        DeviceAddress m_baseAddress = 0;

    private:
        Result allocateBuffer(Size size);
        void destroyBuffer();
        DeviceAddress getBufferAddress() const;
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
