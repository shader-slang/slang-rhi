#include "vk-descriptor-allocator.h"
#include "vk-utils.h"

#include "core/static_vector.h"

namespace rhi::vk {

VkDescriptorPool DescriptorSetAllocator::newPool()
{
    VkDescriptorPoolCreateInfo descriptorPoolInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    static_vector<VkDescriptorPoolSize, 32> poolSizes;
    poolSizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, 1024});
    poolSizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024});
    poolSizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4096});
    poolSizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024});
    poolSizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 256});
    poolSizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 256});
    poolSizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096});
    poolSizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4096});
    poolSizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 4096});
    poolSizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 4096});
    poolSizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 16});
    if (m_api->m_extendedFeatures.inlineUniformBlockFeatures.inlineUniformBlock)
    {
        poolSizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT, 16});
    }
    if (m_api->m_extendedFeatures.accelerationStructureFeatures.accelerationStructure)
    {
        poolSizes.push_back(VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 256});
    }
    descriptorPoolInfo.maxSets = 4096;
    descriptorPoolInfo.poolSizeCount = (uint32_t)poolSizes.size();
    descriptorPoolInfo.pPoolSizes = poolSizes.data();
    descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    VkDescriptorPoolInlineUniformBlockCreateInfo inlineUniformBlockInfo = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_INLINE_UNIFORM_BLOCK_CREATE_INFO
    };
    inlineUniformBlockInfo.maxInlineUniformBlockBindings = 16;
    descriptorPoolInfo.pNext = &inlineUniformBlockInfo;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    SLANG_VK_CHECK(m_api->vkCreateDescriptorPool(m_api->m_device, &descriptorPoolInfo, nullptr, &descriptorPool));
    pools.push_back(descriptorPool);
    return descriptorPool;
}

VulkanDescriptorSet DescriptorSetAllocator::allocate(VkDescriptorSetLayout layout)
{
    VulkanDescriptorSet rs = {};
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = getPool();
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;
    if (m_api->vkAllocateDescriptorSets(m_api->m_device, &allocInfo, &rs.handle) == VK_SUCCESS)
    {
        rs.pool = allocInfo.descriptorPool;
        return rs;
    }
    // If allocation from last pool fails, try all existing pools.
    for (size_t i = 0; i < pools.size() - 1; i++)
    {
        allocInfo.descriptorPool = pools[i];
        if (m_api->vkAllocateDescriptorSets(m_api->m_device, &allocInfo, &rs.handle) == VK_SUCCESS)
        {
            rs.pool = allocInfo.descriptorPool;
            return rs;
        }
    }
    // If we still cannot allocate the descriptor set, add a new pool.
    auto pool = newPool();
    allocInfo.descriptorPool = pool;
    if (m_api->vkAllocateDescriptorSets(m_api->m_device, &allocInfo, &rs.handle) == VK_SUCCESS)
    {
        rs.pool = allocInfo.descriptorPool;
        return rs;
    }
    // Failed to allocate from a new pool, we are in trouble.
    SLANG_RHI_ASSERT_FAILURE("Descriptor set allocation failed.");
    return rs;
}

} // namespace rhi::vk
