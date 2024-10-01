#include "vk-acceleration-structure.h"
#include "vk-device.h"
#include "vk-buffer.h"

namespace rhi::vk {

AccelerationStructureImpl::~AccelerationStructureImpl()
{
    if (m_device)
    {
        m_device->m_api.vkDestroyAccelerationStructureKHR(m_device->m_api.m_device, m_vkHandle, nullptr);
    }
}

Result AccelerationStructureImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkAccelerationStructureKHR;
    outHandle->value = (uint64_t)m_vkHandle;
    return SLANG_OK;
}

AccelerationStructureHandle AccelerationStructureImpl::getHandle()
{
    return AccelerationStructureHandle{m_buffer->getDeviceAddress()};
}

DeviceAddress AccelerationStructureImpl::getDeviceAddress()
{
    return m_buffer->getDeviceAddress();
}

} // namespace rhi::vk
