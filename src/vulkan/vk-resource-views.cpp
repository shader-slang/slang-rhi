#include "vk-resource-views.h"

namespace rhi::vk {

TextureViewImpl::~TextureViewImpl()
{
    m_device->m_api.vkDestroyImageView(m_device->m_api.m_device, m_view, nullptr);
}

Result TextureViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkImageView;
    outHandle->value = (uint64_t)m_view;
    return SLANG_OK;
}

TexelBufferViewImpl::TexelBufferViewImpl(DeviceImpl* device)
    : ResourceViewImpl(ViewType::TexelBuffer, device)
{
}

TexelBufferViewImpl::~TexelBufferViewImpl()
{
    m_device->m_api.vkDestroyBufferView(m_device->m_api.m_device, m_view, nullptr);
}

Result TexelBufferViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkImageView;
    outHandle->value = (uint64_t)m_view;
    return SLANG_OK;
}

PlainBufferViewImpl::PlainBufferViewImpl(DeviceImpl* device)
    : ResourceViewImpl(ViewType::PlainBuffer, device)
{
}

Result PlainBufferViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    return m_buffer->getNativeHandle(outHandle);
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
