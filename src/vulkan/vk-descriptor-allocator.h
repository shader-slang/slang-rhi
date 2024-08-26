// vk-descriptor-allocator.h

#pragma once

#include "vk-api.h"

#include "utils/common.h"

#include <vector>

namespace gfx
{
struct VulkanDescriptorSet
{
    VkDescriptorSet handle;
    VkDescriptorPool pool;
};
class DescriptorSetAllocator
{
public:
    std::vector<VkDescriptorPool> pools;
    const VulkanApi* m_api;
    VkDescriptorPool newPool();
    VkDescriptorPool getPool()
    {
        if (!pools.empty())
            return pools.back();
        return newPool();
    }
    VulkanDescriptorSet allocate(VkDescriptorSetLayout layout);
    void free(VulkanDescriptorSet set)
    {
        m_api->vkFreeDescriptorSets(m_api->m_device, set.pool, 1, &set.handle);
    }
    void reset()
    {
        for (auto pool : pools)
            m_api->vkResetDescriptorPool(m_api->m_device, pool, 0);
    }
    void close()
    {
        for (auto pool : pools)
            m_api->vkDestroyDescriptorPool(m_api->m_device, pool, nullptr);
    }
};
} // namespace gfx
