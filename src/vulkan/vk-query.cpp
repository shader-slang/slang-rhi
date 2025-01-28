#include "vk-query.h"
#include "vk-device.h"
#include "vk-util.h"

namespace rhi::vk {

Result QueryPoolImpl::init(const QueryPoolDesc& desc, DeviceImpl* device)
{
    m_device = device;
    m_pool = VK_NULL_HANDLE;
    VkQueryPoolCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    createInfo.queryCount = desc.count;
    switch (desc.type)
    {
    case QueryType::Timestamp:
        createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        break;
    case QueryType::AccelerationStructureCompactedSize:
        createInfo.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
        break;
    case QueryType::AccelerationStructureSerializedSize:
        createInfo.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR;
        break;
    case QueryType::AccelerationStructureCurrentSize:
        // Vulkan does not support CurrentSize query, will not create actual pools here.
        return SLANG_OK;
    default:
        return SLANG_E_INVALID_ARG;
    }
    SLANG_VK_RETURN_ON_FAIL(m_device->m_api.vkCreateQueryPool(m_device->m_api.m_device, &createInfo, nullptr, &m_pool));

    m_device->_labelObject((uint64_t)m_pool, VK_OBJECT_TYPE_QUERY_POOL, desc.label);

    return SLANG_OK;
}

QueryPoolImpl::~QueryPoolImpl()
{
    m_device->m_api.vkDestroyQueryPool(m_device->m_api.m_device, m_pool, nullptr);
}

Result QueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* data)
{
    if (!m_pool)
    {
        // Vulkan does not support CurrentSize query, return 0 here.
        for (SlangInt i = 0; i < count; i++)
            data[i] = 0;
        return SLANG_OK;
    }

    SLANG_VK_RETURN_ON_FAIL(m_device->m_api.vkGetQueryPoolResults(
        m_device->m_api.m_device,
        m_pool,
        queryIndex,
        count,
        sizeof(uint64_t) * count,
        data,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
    ));
    return SLANG_OK;
}

void _writeTimestamp(VulkanApi* api, VkCommandBuffer vkCmdBuffer, IQueryPool* queryPool, SlangInt index)
{
    auto queryPoolImpl = checked_cast<QueryPoolImpl*>(queryPool);
    api->vkCmdResetQueryPool(vkCmdBuffer, queryPoolImpl->m_pool, (uint32_t)index, 1);
    api->vkCmdWriteTimestamp(vkCmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolImpl->m_pool, (uint32_t)index);
}

Result DeviceImpl::createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool)
{
    RefPtr<QueryPoolImpl> result = new QueryPoolImpl();
    SLANG_RETURN_ON_FAIL(result->init(desc, this));
    returnComPtr(outPool, result);
    return SLANG_OK;
}

} // namespace rhi::vk
