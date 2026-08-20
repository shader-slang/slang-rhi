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

    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    uint32_t m_memoryTypeIndex = 0;
    void* m_mapped = nullptr;
};

} // namespace rhi::vk
