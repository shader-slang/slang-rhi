#pragma once

#include "vk-buffer.h"
#include "core/offset-allocator.h"

#include <mutex>
#include <memory>

namespace rhi::vk {

// A device owns one pair of heaps. Persistent bindless descriptors occupy the
// prefix; command buffers retain allocations from the remaining space until reset.
class DescriptorHeap : public RefObject
{
public:
    struct Allocation
    {
        OffsetAllocator::Allocation resource;
        OffsetAllocator::Allocation sampler;
    };

    DescriptorHeap(DeviceImpl* device)
        : m_device(device)
    {
    }
    ~DescriptorHeap();
    Result initialize(const BindlessDesc& bindless, uint32_t transientCount);
    Result allocate(uint32_t resourceCount, uint32_t samplerCount, Allocation& allocation);
    void free(const Allocation& allocation);
    void bind(VkCommandBuffer commandBuffer);

    Result writeBuffer(uint32_t slot, VkDescriptorType type, BufferImpl* buffer, BufferRange range, Format format);
    Result writeTexture(uint32_t slot, VkDescriptorType type, TextureViewImpl* view);
    Result writeSampler(uint32_t slot, SamplerImpl* sampler);
    Result writeAccelerationStructure(uint32_t slot, AccelerationStructureImpl* as);
    Result encodeSampler(const VkSamplerCreateInfo& info, std::vector<uint8_t>& descriptor);

    uint32_t m_resourceStride = 0;
    uint32_t m_samplerStride = 0;
    uint32_t m_resourcePrefix = 0;
    uint32_t m_samplerPrefix = 0;
    std::vector<uint8_t> m_defaultSampler;

private:
    struct Heap
    {
        VKBufferHandleRAII buffer;
        void* mapped = nullptr;
        VkBindHeapInfoEXT bindInfo = {VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT};
    };
    Result initializeHeap(Heap& heap, uint32_t count, uint32_t stride, bool sampler);
    Result writeResource(uint32_t slot, const VkResourceDescriptorInfoEXT& info);
    DeviceImpl* m_device;
    Heap m_resources;
    Heap m_samplers;
    std::unique_ptr<OffsetAllocator> m_resourceAllocator;
    std::unique_ptr<OffsetAllocator> m_samplerAllocator;
    std::mutex m_mutex;
};

} // namespace rhi::vk
