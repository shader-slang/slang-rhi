#include "vk-api.h"

#include "core/assert.h"
#include "../rhi-shared.h"

#include <vector>

#if SLANG_WINDOWS_FAMILY
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace rhi::vk {

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! VulkanModule !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

Result VulkanModule::init()
{
    if (isInitialized())
    {
        destroy();
    }

#if SLANG_WINDOWS_FAMILY
    HMODULE module = ::LoadLibraryA("vulkan-1.dll");
    m_module = (void*)module;
#elif SLANG_LINUX_FAMILY
    m_module = dlopen("libvulkan.so.1", RTLD_NOW);
#elif SLANG_APPLE_FAMILY
    m_module = dlopen("libvulkan.1.dylib", RTLD_NOW | RTLD_GLOBAL);
#else
#error "Unsupported platform"
#endif

    if (!m_module)
    {
        return SLANG_FAIL;
    }

    return SLANG_OK;
}

PFN_vkVoidFunction VulkanModule::getFunction(const char* name) const
{
    SLANG_RHI_ASSERT(m_module);
    if (!m_module)
    {
        return nullptr;
    }
#if SLANG_WINDOWS_FAMILY
    return (PFN_vkVoidFunction)::GetProcAddress((HMODULE)m_module, name);
#else
    return (PFN_vkVoidFunction)dlsym(m_module, name);
#endif
}

void VulkanModule::destroy()
{
    if (!isInitialized())
    {
        return;
    }

#if SLANG_WINDOWS_FAMILY
    ::FreeLibrary((HMODULE)m_module);
#else
    dlclose(m_module);
#endif
    m_module = nullptr;
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! VulkanApi !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

#define VK_API_CHECK_FUNCTION(x) &&(x != nullptr)
#define VK_API_CHECK_FUNCTIONS(FUNCTION_LIST) true FUNCTION_LIST(VK_API_CHECK_FUNCTION)

bool VulkanApi::areDefined(ProcType type) const
{
    switch (type)
    {
    case ProcType::Global:
        return VK_API_CHECK_FUNCTIONS(VK_API_ALL_GLOBAL_PROCS);
    case ProcType::Instance:
        return VK_API_CHECK_FUNCTIONS(VK_API_ALL_INSTANCE_PROCS);
    case ProcType::Device:
        return VK_API_CHECK_FUNCTIONS(VK_API_DEVICE_PROCS);
    default:
    {
        SLANG_RHI_ASSERT_FAILURE("Unhandled type");
        return false;
    }
    }
}

Result VulkanApi::initGlobalProcs(const VulkanModule& module)
{
#define VK_API_GET_GLOBAL_PROC(x) x = (PFN_##x)module.getFunction(#x);

    // Initialize all the global functions
    VK_API_ALL_GLOBAL_PROCS(VK_API_GET_GLOBAL_PROC)

    if (!areDefined(ProcType::Global))
    {
        return SLANG_FAIL;
    }
    m_module = &module;
    return SLANG_OK;
}

Result VulkanApi::initInstanceProcs(VkInstance instance)
{
    SLANG_RHI_ASSERT(instance && vkGetInstanceProcAddr != nullptr);

#define VK_API_GET_INSTANCE_PROC(x) x = (PFN_##x)vkGetInstanceProcAddr(instance, #x);

    VK_API_ALL_INSTANCE_PROCS(VK_API_GET_INSTANCE_PROC)

    // Get optional
    VK_API_INSTANCE_PROCS_OPT(VK_API_GET_INSTANCE_PROC)

    if (!areDefined(ProcType::Instance))
    {
        return SLANG_FAIL;
    }

    m_instance = instance;
    return SLANG_OK;
}

Result VulkanApi::initPhysicalDevice(VkPhysicalDevice physicalDevice)
{
    SLANG_RHI_ASSERT(m_physicalDevice == VK_NULL_HANDLE);
    m_physicalDevice = physicalDevice;

    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_deviceProperties);
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &m_deviceFeatures);
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_deviceMemoryProperties);

    return SLANG_OK;
}

Result VulkanApi::initDeviceProcs(VkDevice device)
{
    SLANG_RHI_ASSERT(m_instance && device && vkGetDeviceProcAddr != nullptr);

#define VK_API_GET_DEVICE_PROC(x) x = (PFN_##x)vkGetDeviceProcAddr(device, #x);

    VK_API_ALL_DEVICE_PROCS(VK_API_GET_DEVICE_PROC)

    if (!areDefined(ProcType::Device))
    {
        return SLANG_FAIL;
    }

    if (!vkGetBufferDeviceAddressKHR && vkGetBufferDeviceAddressEXT)
        vkGetBufferDeviceAddressKHR = vkGetBufferDeviceAddressEXT;
    if (!vkGetBufferDeviceAddress && vkGetBufferDeviceAddressKHR)
        vkGetBufferDeviceAddress = vkGetBufferDeviceAddressKHR;
    if (!vkGetSemaphoreCounterValue && vkGetSemaphoreCounterValueKHR)
        vkGetSemaphoreCounterValue = vkGetSemaphoreCounterValueKHR;
    if (!vkSignalSemaphore && vkSignalSemaphoreKHR)
        vkSignalSemaphore = vkSignalSemaphoreKHR;
    m_device = device;
    return SLANG_OK;
}

int VulkanApi::findMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties) const
{
    SLANG_RHI_ASSERT(typeBits);

    const int numMemoryTypes = int(m_deviceMemoryProperties.memoryTypeCount);

    // bit holds current test bit against typeBits. Ie bit == 1 << typeBits

    uint32_t bit = 1;
    for (int i = 0; i < numMemoryTypes; ++i, bit += bit)
    {
        const auto& memoryType = m_deviceMemoryProperties.memoryTypes[i];
        if ((typeBits & bit) && (memoryType.propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    // SLANG_RHI_ASSERT_FAILURE("Failed to find a usable memory type");
    return -1;
}

int VulkanApi::findQueue(VkQueueFlags reqFlags) const
{
    SLANG_RHI_ASSERT(m_physicalDevice != VK_NULL_HANDLE);

    uint32_t numQueueFamilies = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &numQueueFamilies, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies;
    queueFamilies.resize(numQueueFamilies);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &numQueueFamilies, queueFamilies.data());

    // Find a queue that can service our needs
    for (int i = 0; i < int(numQueueFamilies); ++i)
    {
        if ((queueFamilies[i].queueFlags & reqFlags) == reqFlags)
        {
            return i;
        }
    }

    return -1;
}

} // namespace rhi::vk
