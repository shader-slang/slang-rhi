#include "vk-descriptor-heap.h"
#include "vk-device.h"
#include "vk-texture.h"
#include "vk-sampler.h"
#include "vk-acceleration-structure.h"
#include "vk-utils.h"

#include <limits>

namespace rhi::vk {

DescriptorHeap::~DescriptorHeap()
{
    for (Heap* heap : {&m_resources, &m_samplers})
        if (heap->mapped)
            m_device->m_api.vkUnmapMemory(m_device->m_device, heap->buffer.m_memory);
}

Result DescriptorHeap::initializeHeap(Heap& heap, uint32_t count, uint32_t stride, bool sampler)
{
    const auto& props = m_device->m_descriptorHeapProperties;
    auto& api = m_device->m_api;
    VkDeviceSize alignment = sampler ? props.samplerHeapAlignment : props.resourceHeapAlignment;
    VkDeviceSize reserved = sampler ? props.minSamplerHeapReservedRange : props.minResourceHeapReservedRange;
    VkDeviceSize size = VkDeviceSize(count) * stride;
    VkDeviceSize reservedOffset = (size + alignment - 1) / alignment * alignment;
    size = reservedOffset + reserved;
    if (size > (sampler ? props.maxSamplerHeapSize : props.maxResourceHeapSize))
        return SLANG_E_OUT_OF_MEMORY;
    SLANG_RETURN_ON_FAIL(heap.buffer.init(
        api, size, VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    ));
    SLANG_VK_RETURN_ON_FAIL(api.vkMapMemory(api.m_device, heap.buffer.m_memory, 0, VK_WHOLE_SIZE, 0, &heap.mapped));
    VkBufferDeviceAddressInfo addressInfo = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    addressInfo.buffer = heap.buffer.m_buffer;
    heap.bindInfo.heapRange = {api.vkGetBufferDeviceAddress(api.m_device, &addressInfo), size};
    heap.bindInfo.reservedRangeOffset = reservedOffset;
    heap.bindInfo.reservedRangeSize = reserved;
    return SLANG_OK;
}

Result DescriptorHeap::initialize(const BindlessDesc& bindless, uint32_t transientCount)
{
    const auto& props = m_device->m_descriptorHeapProperties;
    VkDeviceSize resourceAlignment = std::max(props.bufferDescriptorAlignment, props.imageDescriptorAlignment);
    VkDeviceSize resourceSize = std::max(VkDeviceSize(8), std::max(props.bufferDescriptorSize, props.imageDescriptorSize));
    resourceSize = (resourceSize + resourceAlignment - 1) / resourceAlignment * resourceAlignment;
    VkDeviceSize samplerSize = (props.samplerDescriptorSize + props.samplerDescriptorAlignment - 1) /
                               props.samplerDescriptorAlignment * props.samplerDescriptorAlignment;
    uint64_t resourcePrefix = uint64_t(bindless.bufferCount) + bindless.textureCount +
                              bindless.accelerationStructureCount + bindless.combinedTextureSamplerCount;
    uint64_t samplerPrefix = uint64_t(bindless.samplerCount) + bindless.combinedTextureSamplerCount;
    if (!transientCount || resourceSize > INT32_MAX || samplerSize > INT32_MAX ||
        resourcePrefix + transientCount > UINT32_MAX || samplerPrefix + transientCount > UINT32_MAX ||
        (resourcePrefix + transientCount) * resourceSize > UINT32_MAX ||
        (samplerPrefix + transientCount) * samplerSize > UINT32_MAX)
        return SLANG_E_INVALID_ARG;
    m_resourceStride = uint32_t(resourceSize);
    m_samplerStride = uint32_t(samplerSize);
    m_resourcePrefix = uint32_t(resourcePrefix);
    m_samplerPrefix = uint32_t(samplerPrefix);
    auto availableSlots = [](VkDeviceSize maxSize, VkDeviceSize reserved, VkDeviceSize alignment, uint32_t stride, uint32_t prefix)
    {
        if (maxSize < reserved)
            return uint32_t(0);
        VkDeviceSize slots = ((maxSize - reserved) / alignment * alignment) / stride;
        return slots > prefix ? uint32_t(std::min(slots - prefix, VkDeviceSize(UINT32_MAX))) : 0;
    };
    uint32_t resources = std::min(transientCount, availableSlots(props.maxResourceHeapSize,
        props.minResourceHeapReservedRange, props.resourceHeapAlignment, m_resourceStride, m_resourcePrefix));
    uint32_t samplers = std::min(transientCount, availableSlots(props.maxSamplerHeapSize,
        props.minSamplerHeapReservedRange, props.samplerHeapAlignment, m_samplerStride, m_samplerPrefix));
    if (!resources || !samplers)
        return SLANG_E_OUT_OF_MEMORY;
    SLANG_RETURN_ON_FAIL(initializeHeap(m_resources, m_resourcePrefix + resources, m_resourceStride, false));
    SLANG_RETURN_ON_FAIL(initializeHeap(m_samplers, m_samplerPrefix + samplers, m_samplerStride, true));
    m_resourceAllocator = std::make_unique<OffsetAllocator>(resources);
    m_samplerAllocator = std::make_unique<OffsetAllocator>(samplers);
    return SLANG_OK;
}

Result DescriptorHeap::allocate(uint32_t resourceCount, uint32_t samplerCount, Allocation& allocation)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (resourceCount)
        allocation.resource = m_resourceAllocator->allocate(resourceCount);
    if (resourceCount && !allocation.resource)
        return SLANG_E_OUT_OF_MEMORY;
    if (samplerCount)
        allocation.sampler = m_samplerAllocator->allocate(samplerCount);
    if (samplerCount && !allocation.sampler)
    {
        if (allocation.resource)
            m_resourceAllocator->free(allocation.resource);
        allocation.resource = {};
        return SLANG_E_OUT_OF_MEMORY;
    }
    return SLANG_OK;
}

void DescriptorHeap::free(const Allocation& allocation)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (allocation.resource)
        m_resourceAllocator->free(allocation.resource);
    if (allocation.sampler)
        m_samplerAllocator->free(allocation.sampler);
}

void DescriptorHeap::bind(VkCommandBuffer commandBuffer)
{
    m_device->m_api.vkCmdBindResourceHeapEXT(commandBuffer, &m_resources.bindInfo);
    m_device->m_api.vkCmdBindSamplerHeapEXT(commandBuffer, &m_samplers.bindInfo);
}

Result DescriptorHeap::writeResource(uint32_t slot, const VkResourceDescriptorInfoEXT& info)
{
    VkHostAddressRangeEXT destination = {static_cast<uint8_t*>(m_resources.mapped) + size_t(slot) * m_resourceStride,
                                         m_resourceStride};
    SLANG_VK_RETURN_ON_FAIL(m_device->m_api.vkWriteResourceDescriptorsEXT(m_device->m_device, 1, &info, &destination));
    return SLANG_OK;
}

Result DescriptorHeap::writeBuffer(uint32_t slot, VkDescriptorType type, BufferImpl* buffer, BufferRange range, Format format)
{
    VkResourceDescriptorInfoEXT info = {VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT};
    info.type = type;
    VkDeviceAddressRangeEXT address = {};
    VkTexelBufferDescriptorInfoEXT texel = {VK_STRUCTURE_TYPE_TEXEL_BUFFER_DESCRIPTOR_INFO_EXT};
    if (buffer)
    {
        address.address = buffer->getDeviceAddress() + range.offset;
        address.size = range.size == kEntireBuffer.size ? buffer->m_desc.size - range.offset : range.size;
        if (format == Format::Undefined)
            info.data.pAddressRange = &address;
        else
        {
            texel.format = getVkFormat(format);
            texel.addressRange = address;
            info.data.pTexelBuffer = &texel;
        }
    }
    return writeResource(slot, info);
}

Result DescriptorHeap::writeTexture(uint32_t slot, VkDescriptorType type, TextureViewImpl* view)
{
    VkResourceDescriptorInfoEXT info = {VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT};
    info.type = type;
    VkImageViewCreateInfo viewInfo = {};
    VkImageDescriptorInfoEXT image = {VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT};
    if (view)
    {
        viewInfo = view->m_texture->getViewCreateInfo(view->m_desc.format, view->m_desc.aspect, view->m_desc.subresourceRange, false);
        image.pView = &viewInfo;
        image.layout = type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        info.data.pImage = &image;
    }
    return writeResource(slot, info);
}

Result DescriptorHeap::encodeSampler(const VkSamplerCreateInfo& info, std::vector<uint8_t>& descriptor)
{
    descriptor.resize(m_samplerStride);
    VkHostAddressRangeEXT destination = {descriptor.data(), descriptor.size()};
    SLANG_VK_RETURN_ON_FAIL(m_device->m_api.vkWriteSamplerDescriptorsEXT(m_device->m_device, 1, &info, &destination));
    return SLANG_OK;
}

Result DescriptorHeap::writeSampler(uint32_t slot, SamplerImpl* sampler)
{
    const auto& descriptor = sampler ? sampler->m_heapDescriptor : m_defaultSampler;
    memcpy(static_cast<uint8_t*>(m_samplers.mapped) + size_t(slot) * m_samplerStride, descriptor.data(), descriptor.size());
    return SLANG_OK;
}

Result DescriptorHeap::writeAccelerationStructure(uint32_t slot, AccelerationStructureImpl* as)
{
    VkResourceDescriptorInfoEXT info = {VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT};
    info.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    VkDeviceAddressRangeEXT address = {};
    if (as)
    {
        address.address = as->getAccelerationStructureDeviceAddress();
        info.data.pAddressRange = &address;
    }
    Result result = writeResource(slot, info);
    fprintf(stderr, "AS descriptor address %llx raw %llx\n", address.address, *reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(m_resources.mapped) + size_t(slot) * m_resourceStride));
    return result;
}

} // namespace rhi::vk
