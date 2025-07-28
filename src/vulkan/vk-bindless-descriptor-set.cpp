#include "vk-bindless-descriptor-set.h"
#include "vk-device.h"
#include "vk-buffer.h"
#include "vk-texture.h"
#include "vk-sampler.h"
#include "vk-acceleration-structure.h"
#include "vk-utils.h"

#include "core/static_vector.h"

namespace rhi::vk {

// Default binding locations for bindless descriptor set.
static constexpr uint32_t kSamplerBinding = 0;
static constexpr uint32_t kCombinedImageSamplerBinding = 1;
static constexpr uint32_t kResourceBinding = 2;

BindlessDescriptorSet::BindlessDescriptorSet(DeviceImpl* device, const BindlessDesc& desc)
    : m_device(device)
    , m_desc(desc)
{
}

BindlessDescriptorSet::~BindlessDescriptorSet()
{
    const auto& api = m_device->m_api;

    if (m_descriptorSet)
    {
        api.vkFreeDescriptorSets(api.m_device, m_descriptorPool, 1, &m_descriptorSet);
    }
    if (m_descriptorSetLayout)
    {
        api.vkDestroyDescriptorSetLayout(api.m_device, m_descriptorSetLayout, nullptr);
    }
    if (m_descriptorPool)
    {
        api.vkDestroyDescriptorPool(api.m_device, m_descriptorPool, nullptr);
    }
}

Result BindlessDescriptorSet::initialize()
{
    const auto& api = m_device->m_api;

    m_firstTextureHandle = m_desc.bufferCount;
    m_firstAccelerationStructureHandle = m_desc.bufferCount + m_desc.textureCount;

    // Create descriptor pool
    {
        VkDescriptorPoolCreateInfo createInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        static_vector<VkDescriptorPoolSize, 16> poolSizes;
        poolSizes.push_back({VK_DESCRIPTOR_TYPE_SAMPLER, m_desc.samplerCount});
        poolSizes.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1});
        poolSizes.push_back(
            {VK_DESCRIPTOR_TYPE_MUTABLE_EXT,
             m_desc.bufferCount + m_desc.textureCount + m_desc.accelerationStructureCount}
        );
        createInfo.maxSets = 1;
        createInfo.poolSizeCount = (uint32_t)poolSizes.size();
        createInfo.pPoolSizes = poolSizes.data();
        createInfo.flags =
            VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

        SLANG_VK_RETURN_ON_FAIL(api.vkCreateDescriptorPool(api.m_device, &createInfo, nullptr, &m_descriptorPool));
    }

    // Create descriptor set layout
    {
        VkDescriptorSetLayoutBinding bindings[3] = {};
        VkDescriptorBindingFlags flags[3] = {};
        VkMutableDescriptorTypeListEXT mutableDescriptorTypeLists[3] = {};

        static_vector<VkDescriptorType, 16> mutableDescriptorTypes;
        mutableDescriptorTypes.push_back(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        mutableDescriptorTypes.push_back(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        mutableDescriptorTypes.push_back(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
        mutableDescriptorTypes.push_back(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
        mutableDescriptorTypes.push_back(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        mutableDescriptorTypes.push_back(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        if (api.m_extendedFeatures.accelerationStructureFeatures.accelerationStructure)
        {
            mutableDescriptorTypes.push_back(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
        }

        // Binding 0 is for samplers
        bindings[0].binding = kSamplerBinding;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        bindings[0].descriptorCount = m_desc.samplerCount;
        bindings[0].stageFlags = VK_SHADER_STAGE_ALL;
        bindings[0].pImmutableSamplers = nullptr;
        flags[0] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

        // Binding 1 is combined image samplers (unused)
        bindings[1].binding = kCombinedImageSamplerBinding;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        flags[1] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        mutableDescriptorTypeLists[1] = {};

        // Binding 2 is for resource descriptors
        bindings[2].binding = kResourceBinding;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_MUTABLE_EXT;
        bindings[2].descriptorCount = m_desc.bufferCount + m_desc.textureCount + m_desc.accelerationStructureCount;
        bindings[2].stageFlags = VK_SHADER_STAGE_ALL;
        flags[2] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        mutableDescriptorTypeLists[2] = {};
        mutableDescriptorTypeLists[2].descriptorTypeCount = mutableDescriptorTypes.size();
        mutableDescriptorTypeLists[2].pDescriptorTypes = mutableDescriptorTypes.data();

        VkDescriptorSetLayoutBindingFlagsCreateInfo flagsCreateInfo = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO
        };
        flagsCreateInfo.bindingCount = 3;
        flagsCreateInfo.pBindingFlags = flags;

        VkMutableDescriptorTypeCreateInfoEXT mutableCreateInfo = {
            VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT
        };
        mutableCreateInfo.pNext = &flagsCreateInfo;
        mutableCreateInfo.mutableDescriptorTypeListCount = 3;
        mutableCreateInfo.pMutableDescriptorTypeLists = mutableDescriptorTypeLists;

        VkDescriptorSetLayoutCreateInfo createInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        createInfo.pNext = &mutableCreateInfo;
        createInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        createInfo.bindingCount = 3;
        createInfo.pBindings = bindings;

        SLANG_VK_RETURN_ON_FAIL(
            api.vkCreateDescriptorSetLayout(api.m_device, &createInfo, nullptr, &m_descriptorSetLayout)
        );
    }

    // Create descriptor set
    {
        VkDescriptorSetAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_descriptorSetLayout;
        SLANG_VK_RETURN_ON_FAIL(api.vkAllocateDescriptorSets(api.m_device, &allocInfo, &m_descriptorSet));
    }

    m_bufferAllocator.capacity = m_desc.bufferCount;
    m_textureAllocator.capacity = m_desc.textureCount;
    m_samplerAllocator.capacity = m_desc.samplerCount;
    m_accelerationStructureAllocator.capacity = m_desc.accelerationStructureCount;

    return SLANG_OK;
}

Result BindlessDescriptorSet::allocBufferHandle(
    IBuffer* buffer,
    DescriptorHandleAccess access,
    Format format,
    BufferRange range,
    DescriptorHandle* outHandle
)
{
    uint32_t slot;
    SLANG_RETURN_ON_FAIL(m_bufferAllocator.allocate(&slot));

    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);

    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_descriptorSet;
    write.dstBinding = kResourceBinding;
    write.descriptorCount = 1;
    write.dstArrayElement = slot;

    switch (access)
    {
    case DescriptorHandleAccess::Read:
        outHandle->type = DescriptorHandleType::Buffer;
        break;
    case DescriptorHandleAccess::ReadWrite:
        outHandle->type = DescriptorHandleType::RWBuffer;
        break;
    default:
        return SLANG_E_INVALID_ARG;
    }

    VkDescriptorBufferInfo bufferInfo = {};
    VkBufferView bufferView = {};

    if (format == Format::Undefined)
    {
        // read-only could be VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferInfo.buffer = bufferImpl->m_buffer.m_buffer;
        bufferInfo.offset = range.offset;
        bufferInfo.range = range.size;
        write.pBufferInfo = &bufferInfo;
    }
    else
    {
        write.descriptorType = access == DescriptorHandleAccess::Read ? VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
                                                                      : VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        bufferView = bufferImpl->getView(format, range);
        write.pTexelBufferView = &bufferView;
    }

    const auto& api = m_device->m_api;
    api.vkUpdateDescriptorSets(api.m_device, 1, &write, 0, nullptr);

    outHandle->value = slot;

    return SLANG_OK;
}

Result BindlessDescriptorSet::allocTextureHandle(
    ITextureView* textureView,
    DescriptorHandleAccess access,
    DescriptorHandle* outHandle
)
{
    uint32_t slot;
    SLANG_RETURN_ON_FAIL(m_textureAllocator.allocate(&slot));

    TextureViewImpl* textureViewImpl = checked_cast<TextureViewImpl*>(textureView);

    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_descriptorSet;
    write.dstBinding = kResourceBinding;
    write.descriptorCount = 1;
    write.dstArrayElement = m_firstTextureHandle + slot;

    switch (access)
    {
    case DescriptorHandleAccess::Read:
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        outHandle->type = DescriptorHandleType::Texture;
        break;
    case DescriptorHandleAccess::ReadWrite:
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        outHandle->type = DescriptorHandleType::RWTexture;
        break;
    default:
        return SLANG_E_INVALID_ARG;
    }

    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageView = textureViewImpl->getView().imageView;
    imageInfo.imageLayout =
        access == DescriptorHandleAccess::Read ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
    write.pImageInfo = &imageInfo;

    const auto& api = m_device->m_api;
    api.vkUpdateDescriptorSets(api.m_device, 1, &write, 0, nullptr);

    outHandle->value = m_firstTextureHandle + slot;

    return SLANG_OK;
}

Result BindlessDescriptorSet::allocSamplerHandle(ISampler* sampler, DescriptorHandle* outHandle)
{
    uint32_t slot;
    SLANG_RETURN_ON_FAIL(m_samplerAllocator.allocate(&slot));

    SamplerImpl* samplerImpl = checked_cast<SamplerImpl*>(sampler);

    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_descriptorSet;
    write.dstBinding = kSamplerBinding;
    write.descriptorCount = 1;
    write.dstArrayElement = slot;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;

    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfo.sampler = samplerImpl->m_sampler;
    write.pImageInfo = &imageInfo;

    const auto& api = m_device->m_api;
    api.vkUpdateDescriptorSets(api.m_device, 1, &write, 0, nullptr);

    outHandle->type = DescriptorHandleType::Sampler;
    outHandle->value = slot;

    return SLANG_OK;
}

Result BindlessDescriptorSet::allocAccelerationStructureHandle(
    IAccelerationStructure* accelerationStructure,
    DescriptorHandle* outHandle
)
{
    uint32_t slot;
    SLANG_RETURN_ON_FAIL(m_accelerationStructureAllocator.allocate(&slot));

    AccelerationStructureImpl* asImpl = checked_cast<AccelerationStructureImpl*>(accelerationStructure);

    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_descriptorSet;
    write.dstBinding = kResourceBinding;
    write.descriptorCount = 1;
    write.dstArrayElement = m_firstAccelerationStructureHandle + slot;
    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    VkWriteDescriptorSetAccelerationStructureKHR writeAS = {
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR
    };
    writeAS.accelerationStructureCount = 1;
    writeAS.pAccelerationStructures = &asImpl->m_vkHandle;
    write.pNext = &writeAS;

    const auto& api = m_device->m_api;
    api.vkUpdateDescriptorSets(api.m_device, 1, &write, 0, nullptr);

    outHandle->type = DescriptorHandleType::AccelerationStructure;
    outHandle->value = m_firstAccelerationStructureHandle + slot;

    return SLANG_OK;
}

Result BindlessDescriptorSet::freeHandle(const DescriptorHandle& handle)
{
    switch (handle.type)
    {
    case DescriptorHandleType::Buffer:
    case DescriptorHandleType::RWBuffer:
        return m_bufferAllocator.free(handle.value);
    case DescriptorHandleType::Texture:
    case DescriptorHandleType::RWTexture:
        return m_textureAllocator.free(handle.value - m_firstTextureHandle);
    case DescriptorHandleType::Sampler:
        return m_samplerAllocator.free(handle.value);
    case DescriptorHandleType::AccelerationStructure:
        return m_accelerationStructureAllocator.free(handle.value - m_firstAccelerationStructureHandle);
    default:
        return SLANG_E_INVALID_ARG;
    }
}

} // namespace rhi::vk
