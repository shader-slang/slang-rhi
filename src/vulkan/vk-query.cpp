#include "vk-query.h"
#include "vk-device.h"
#include "vk-utils.h"

namespace rhi::vk {

Result QueryPoolImpl::init()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    m_pool = VK_NULL_HANDLE;
    VkQueryPoolCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    createInfo.queryCount = m_desc.count;
    switch (m_desc.type)
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
    SLANG_VK_RETURN_ON_FAIL(device->m_api.vkCreateQueryPool(device->m_api.m_device, &createInfo, nullptr, &m_pool));

    device->_labelObject((uint64_t)m_pool, VK_OBJECT_TYPE_QUERY_POOL, m_desc.label);

    return SLANG_OK;
}

QueryPoolImpl::QueryPoolImpl(Device* device, const QueryPoolDesc& desc)
    : QueryPool(device, desc)
{
}

QueryPoolImpl::~QueryPoolImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    device->m_api.vkDestroyQueryPool(device->m_api.m_device, m_pool, nullptr);
}

Result QueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* data)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    SLANG_VK_RETURN_ON_FAIL(device->m_api.vkGetQueryPoolResults(
        device->m_api.m_device,
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

void _writeTimestamp(VulkanApi* api, VkCommandBuffer vkCmdBuffer, IQueryPool* queryPool, uint32_t index)
{
    auto queryPoolImpl = checked_cast<QueryPoolImpl*>(queryPool);
    api->vkCmdResetQueryPool(vkCmdBuffer, queryPoolImpl->m_pool, index, 1);
    api->vkCmdWriteTimestamp(vkCmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPoolImpl->m_pool, index);
}

Result DeviceImpl::createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool)
{
    RefPtr<QueryPoolImpl> result = new QueryPoolImpl(this, desc);
    SLANG_RETURN_ON_FAIL(result->init());
    returnComPtr(outPool, result);
    return SLANG_OK;
}

} // namespace rhi::vk
