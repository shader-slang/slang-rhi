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
        device->m_bindlessDescriptorSet->freeHandle(m_descriptorHandle.get());
    }

    device->m_cpuSamplerHeap->free(m_descriptor);
}

void SamplerImpl::deleteThis()
{
    getDevice<DeviceImpl>()->deferDelete(this);
}

Result SamplerImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12CpuDescriptorHandle;
    outHandle->value = m_descriptor.cpuHandle.ptr;
    return SLANG_OK;
}

Result SamplerImpl::getDescriptorHandle(DescriptorHandle* outHandle)
{
    if (m_descriptorHandle.tryGet(outHandle))
    {
        return SLANG_OK;
    }

    DeviceImpl* device = getDevice<DeviceImpl>();

    if (!device->m_bindlessDescriptorSet)
    {
        return SLANG_E_NOT_AVAILABLE;
    }

    std::lock_guard<std::mutex> lock(device->m_samplerDescriptorMutex);

    if (!m_descriptorHandle)
    {
        DescriptorHandle tmp;
        SLANG_RETURN_ON_FAIL(device->m_bindlessDescriptorSet->allocSamplerHandle(this, &tmp));
        m_descriptorHandle.publish(tmp);
    }

    *outHandle = m_descriptorHandle.get();
    return SLANG_OK;
}

} // namespace rhi::d3d12
