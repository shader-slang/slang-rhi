#pragma once

#include "vk-base.h"
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

    /// Dedicated VMA allocation backing the heap.
    VmaAllocation m_vmaAllocation = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    uint32_t m_memoryTypeIndex = 0;
    void* m_mapped = nullptr;
};

} // namespace rhi::vk
