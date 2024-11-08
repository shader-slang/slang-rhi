#include "vk-fence.h"
#include "vk-device.h"
#include "vk-util.h"

namespace rhi::vk {

FenceImpl::FenceImpl(DeviceImpl* device)
    : m_device(device)
{
}

FenceImpl::~FenceImpl()
{
    if (m_semaphore)
    {
        m_device->m_api.vkDestroySemaphore(m_device->m_api.m_device, m_semaphore, nullptr);
    }
}

Result FenceImpl::init(const FenceDesc& desc)
{
    if (!m_device->m_api.m_extendedFeatures.vulkan12Features.timelineSemaphore)
        return SLANG_E_NOT_AVAILABLE;

    VkSemaphoreTypeCreateInfo timelineCreateInfo;
    timelineCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineCreateInfo.pNext = nullptr;
    timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineCreateInfo.initialValue = desc.initialValue;

    VkSemaphoreCreateInfo createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.pNext = &timelineCreateInfo;
    createInfo.flags = 0;

#if SLANG_WINDOWS_FAMILY
    VkExportSemaphoreWin32HandleInfoKHR exportSemaphoreWin32HandleInfoKHR;
#endif
    VkExportSemaphoreCreateInfoKHR exportSemaphoreCreateInfo;
    if (desc.isShared)
    {
#if SLANG_WINDOWS_FAMILY
        exportSemaphoreWin32HandleInfoKHR.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR;
        exportSemaphoreWin32HandleInfoKHR.pNext = timelineCreateInfo.pNext;
        exportSemaphoreWin32HandleInfoKHR.pAttributes = nullptr;
        exportSemaphoreWin32HandleInfoKHR.dwAccess = GENERIC_ALL;
        exportSemaphoreWin32HandleInfoKHR.name = (LPCWSTR) nullptr;
#endif
        exportSemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO_KHR;
#if SLANG_WINDOWS_FAMILY
        exportSemaphoreCreateInfo.pNext = &exportSemaphoreWin32HandleInfoKHR;
        exportSemaphoreCreateInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
        exportSemaphoreCreateInfo.pNext = timelineCreateInfo.pNext;
        exportSemaphoreCreateInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
        timelineCreateInfo.pNext = &exportSemaphoreCreateInfo;
    }

    SLANG_VK_RETURN_ON_FAIL(
        m_device->m_api.vkCreateSemaphore(m_device->m_api.m_device, &createInfo, nullptr, &m_semaphore)
    );

    m_device->_labelObject((uint64_t)m_semaphore, VK_OBJECT_TYPE_FENCE, desc.label);

    return SLANG_OK;
}

Result FenceImpl::getCurrentValue(uint64_t* outValue)
{
    SLANG_VK_RETURN_ON_FAIL(m_device->m_api.vkGetSemaphoreCounterValue(m_device->m_api.m_device, m_semaphore, outValue)
    );
    return SLANG_OK;
}

Result FenceImpl::setCurrentValue(uint64_t value)
{
    uint64_t currentValue = 0;
    SLANG_VK_RETURN_ON_FAIL(
        m_device->m_api.vkGetSemaphoreCounterValue(m_device->m_api.m_device, m_semaphore, &currentValue)
    );
    if (currentValue < value)
    {
        VkSemaphoreSignalInfo signalInfo;
        signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
        signalInfo.pNext = nullptr;
        signalInfo.semaphore = m_semaphore;
        signalInfo.value = value;

        SLANG_VK_RETURN_ON_FAIL(m_device->m_api.vkSignalSemaphore(m_device->m_api.m_device, &signalInfo));
    }
    return SLANG_OK;
}

Result FenceImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkSemaphore;
    outHandle->value = (uint64_t)m_semaphore;
    return SLANG_OK;
}

Result FenceImpl::getSharedHandle(NativeHandle* outHandle)
{
    // Check if a shared handle already exists.
    if (sharedHandle)
    {
        *outHandle = sharedHandle;
        return SLANG_OK;
    }

#if SLANG_WINDOWS_FAMILY
    VkSemaphoreGetWin32HandleInfoKHR handleInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR};
    handleInfo.pNext = nullptr;
    handleInfo.semaphore = m_semaphore;
    handleInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    SLANG_VK_RETURN_ON_FAIL(m_device->m_api.vkGetSemaphoreWin32HandleKHR(
        m_device->m_api.m_device,
        &handleInfo,
        (HANDLE*)&sharedHandle.value
    ));
    sharedHandle.type = NativeHandleType::Win32;
#else
    VkSemaphoreGetFdInfoKHR fdInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
    fdInfo.pNext = nullptr;
    fdInfo.semaphore = m_semaphore;
    fdInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

    SLANG_VK_RETURN_ON_FAIL(
        m_device->m_api.vkGetSemaphoreFdKHR(m_device->m_api.m_device, &fdInfo, (int*)&sharedHandle.value)
    );
    sharedHandle.type = NativeHandleType::FileDescriptor;
#endif
    *outHandle = sharedHandle;
    return SLANG_OK;
}

Result DeviceImpl::createFence(const FenceDesc& desc, IFence** outFence)
{
    RefPtr<FenceImpl> fence = new FenceImpl(this);
    SLANG_RETURN_ON_FAIL(fence->init(desc));
    returnComPtr(outFence, fence);
    return SLANG_OK;
}

} // namespace rhi::vk
