#pragma once

#include "vk-base.h"

namespace rhi::vk {

class QueryPoolImpl : public QueryPool
{
public:
    VkQueryPool m_pool;

    QueryPoolImpl(Device* device, const QueryPoolDesc& desc);
    ~QueryPoolImpl();

    Result init();

    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(uint32_t queryIndex, uint32_t count, uint64_t* data) override;
};

void _writeTimestamp(VulkanApi* api, VkCommandBuffer vkCmdBuffer, IQueryPool* queryPool, uint32_t index);

} // namespace rhi::vk
