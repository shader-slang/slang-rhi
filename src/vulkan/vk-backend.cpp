#include "vk-backend.h"
#include "vk-device.h"
#include "vk-utils.h"
#include "../reference.h"

#include "core/deferred.h"
#include "core/string.h"

namespace rhi::vk {

std::span<const AdapterImpl> BackendImpl::getAdapters()
{
    ensureAdapters();
    return m_adapters;
}

IAdapter* BackendImpl::getAdapter(uint32_t index)
{
    ensureAdapters();
    return index < m_adapters.size() ? &m_adapters[index] : nullptr;
}

Result BackendImpl::createDevice(const DeviceDesc& desc, IDevice** outDevice)
{
    RefPtr<DeviceImpl> result = new DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(desc, this));
    returnComPtr(outDevice, result);
    return SLANG_OK;
}

Result BackendImpl::enumerateAdapters()
{
    VulkanModule module;
    SLANG_RETURN_ON_FAIL(module.init());
    SLANG_RHI_DEFERRED({ module.destroy(); });

    VulkanApi api;
    SLANG_RETURN_ON_FAIL(api.initGlobalProcs(module));

    VkInstanceCreateInfo instanceCreateInfo = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    const char* instanceExtensions[] = {
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
#if SLANG_APPLE_FAMILY
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
#endif
    };
    instanceCreateInfo.enabledExtensionCount = SLANG_COUNT_OF(instanceExtensions);
    instanceCreateInfo.ppEnabledExtensionNames = &instanceExtensions[0];
#if SLANG_APPLE_FAMILY
    instanceCreateInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    VkInstance instance;
    SLANG_VK_RETURN_ON_FAIL(api.vkCreateInstance(&instanceCreateInfo, nullptr, &instance));
    SLANG_RHI_DEFERRED({ api.vkDestroyInstance(instance, nullptr); });

    // This will fail due to not loading any extensions.
    api.initInstanceProcs(instance);

    if (!(api.vkEnumeratePhysicalDevices && api.vkGetPhysicalDeviceProperties2 &&
          api.vkEnumerateDeviceExtensionProperties))
    {
        return SLANG_FAIL;
    }

    uint32_t physicalDeviceCount = 0;
    SLANG_VK_RETURN_ON_FAIL(api.vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr));
    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    SLANG_VK_RETURN_ON_FAIL(api.vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data()));

    for (const auto& physicalDevice : physicalDevices)
    {
        VkPhysicalDeviceIDProperties idProps = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        props.pNext = &idProps;
        SLANG_RHI_ASSERT(api.vkGetPhysicalDeviceProperties2);
        api.vkGetPhysicalDeviceProperties2(physicalDevice, &props);

        AdapterInfo info = {};
        info.deviceType = DeviceType::Vulkan;
        switch (props.properties.deviceType)
        {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            info.adapterType = AdapterType::Discrete;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            info.adapterType = AdapterType::Integrated;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            info.adapterType = AdapterType::Software;
            break;
        default:
            info.adapterType = AdapterType::Unknown;
            break;
        }
        string::copy_safe(info.name, sizeof(info.name), props.properties.deviceName);
        info.vendorID = props.properties.vendorID;
        info.deviceID = props.properties.deviceID;
        info.luid = getAdapterLUID(idProps);

        AdapterImpl adapter;
        adapter.m_info = info;
        memcpy(adapter.m_deviceUUID, idProps.deviceUUID, VK_UUID_SIZE);

        m_adapters.push_back(adapter);
    }

    // Mark default adapter (prefer discrete if available).
    markDefaultAdapter(m_adapters);

    return SLANG_OK;
}

} // namespace rhi::vk

namespace rhi {

Result createVKBackend(Backend** outBackend)
{
    RefPtr<vk::BackendImpl> backend = new vk::BackendImpl();
    returnRefPtr(outBackend, backend);
    return SLANG_OK;
}

} // namespace rhi
