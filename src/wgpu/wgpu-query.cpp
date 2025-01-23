#include "wgpu-query.h"
#include "wgpu-device.h"

namespace rhi::wgpu {

QueryPoolImpl::~QueryPoolImpl()
{
    if (m_querySet)
    {
        m_device->m_ctx.api.wgpuQuerySetRelease(m_querySet);
    }
}

Result QueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* data)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

Result DeviceImpl::createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool)
{
    RefPtr<QueryPoolImpl> pool = new QueryPoolImpl();
    pool->m_device = this;
    pool->m_desc = desc;

    WGPUQuerySetDescriptor querySetDesc = {};
    querySetDesc.count = desc.count;
    querySetDesc.type = WGPUQueryType_Timestamp;
    pool->m_querySet = m_ctx.api.wgpuDeviceCreateQuerySet(m_ctx.device, &querySetDesc);
    if (!pool->m_querySet)
    {
        return SLANG_FAIL;
    }
    returnComPtr(outPool, pool);
    return SLANG_OK;
}

} // namespace rhi::wgpu
