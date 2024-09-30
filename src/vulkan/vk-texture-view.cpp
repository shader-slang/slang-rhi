#include "vk-texture-view.h"

namespace rhi::vk {

Result TextureViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    return SLANG_E_NOT_AVAILABLE;
}

TextureSubresourceView TextureViewImpl::getView()
{
    return m_texture->getView(m_desc.format, m_desc.aspect, m_desc.subresourceRange);
}

DeviceAddress AccelerationStructureImpl::getDeviceAddress()
{
    return m_buffer->getDeviceAddress() + m_offset;
}

Result AccelerationStructureImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkAccelerationStructureKHR;
    outHandle->value = (uint64_t)m_vkHandle;
    return SLANG_OK;
}

AccelerationStructureImpl::~AccelerationStructureImpl()
{
    if (m_device)
    {
        m_device->m_api.vkDestroyAccelerationStructureKHR(m_device->m_api.m_device, m_vkHandle, nullptr);
    }
}

} // namespace rhi::vk
