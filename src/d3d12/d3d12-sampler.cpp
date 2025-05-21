#include "d3d12-sampler.h"
#include "d3d12-device.h"

namespace rhi::d3d12 {

SamplerImpl::SamplerImpl(Device* device, const SamplerDesc& desc)
    : Sampler(device, desc)
{
}

SamplerImpl::~SamplerImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    if (m_descriptorHandle)
    {
        device->m_bindlessDescriptorSet->freeHandle(m_descriptorHandle);
    }

    device->m_cpuSamplerHeap->free(m_descriptor);
}

Result SamplerImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12CpuDescriptorHandle;
    outHandle->value = m_descriptor.cpuHandle.ptr;
    return SLANG_OK;
}

Result SamplerImpl::getDescriptorHandle(DescriptorHandle* outHandle)
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    if (!device->m_bindlessDescriptorSet)
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    if (!m_descriptorHandle)
    {
        SLANG_RETURN_FALSE_ON_FAIL(device->m_bindlessDescriptorSet->allocSamplerHandle(this, &m_descriptorHandle));
    }
    *outHandle = m_descriptorHandle;
    return SLANG_OK;
}

} // namespace rhi::d3d12
