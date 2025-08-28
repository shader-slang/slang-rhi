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

        DeviceAddress offsetToAddress(Size offset) override;

        VKBufferHandleRAII m_buffer;
        DeviceImpl* m_device;
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
