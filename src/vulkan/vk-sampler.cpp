#include "vk-sampler.h"

namespace rhi::vk {

SamplerImpl::SamplerImpl(DeviceImpl* device)
    : m_device(device)
{
}

SamplerImpl::~SamplerImpl()
{
    m_device->m_api.vkDestroySampler(m_device->m_api.m_device, m_sampler, nullptr);
}

Result SamplerImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkSampler;
    outHandle->value = (uint64_t)m_sampler;
    return SLANG_OK;
}

} // namespace rhi::vk
