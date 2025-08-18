#include "webgpu-query.h"
#include "webgpu-device.h"

namespace rhi::webgpu {

QueryPoolImpl::QueryPoolImpl(Device* device, const QueryPoolDesc& desc)
    : QueryPool(device, desc)
{
}

QueryPoolImpl::~QueryPoolImpl()
{
    if (m_querySet)
    {
        getDevice<DeviceImpl>()->m_ctx.api.webgpuQuerySetRelease(m_querySet);
    }
}

Result QueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* data)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

Result DeviceImpl::createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool)
{
    RefPtr<QueryPoolImpl> pool = new QueryPoolImpl(this, desc);

    WebGPUQuerySetDescriptor querySetDesc = {};
    querySetDesc.count = desc.count;
    querySetDesc.type = WebGPUQueryType_Timestamp;
    pool->m_querySet = m_ctx.api.webgpuDeviceCreateQuerySet(m_ctx.device, &querySetDesc);
    if (!pool->m_querySet)
    {
        return SLANG_FAIL;
    }
    returnComPtr(outPool, pool);
    return SLANG_OK;
}

} // namespace rhi::webgpu
