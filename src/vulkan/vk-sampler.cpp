#include "vk-sampler.h"

namespace rhi::vk {

SamplerImpl::SamplerImpl(RendererBase* device, const SamplerDesc& desc)
    : Sampler(device, desc)
{
}

SamplerImpl::~SamplerImpl()
{
    DeviceImpl* device = static_cast<DeviceImpl*>(m_device.get());
    device->m_api.vkDestroySampler(device->m_api.m_device, m_sampler, nullptr);
}

Result SamplerImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkSampler;
    outHandle->value = (uint64_t)m_sampler;
    return SLANG_OK;
}

} // namespace rhi::vk
