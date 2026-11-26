#pragma once

#include "vk-base.h"
#include "vk-memory-allocator.h"
#include "../resource-heap.h"

namespace rhi::vk {

class ResourceHeapImpl : public ResourceHeap
{
public:
    ResourceHeapImpl(Device* device, const ResourceHeapDesc& desc);
    ~ResourceHeapImpl();

    Result init();

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual bool isCompatible(const ResourceMemoryRequirements& requirements) const override;

    /// Dedicated memory backing the heap (persistently mapped for Upload and ReadBack heaps).
    MemoryAllocation m_allocation;
};

} // namespace rhi::vk
