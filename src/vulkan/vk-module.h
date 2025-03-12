#pragma once

#include <slang-com-helper.h>
#include <slang-rhi.h>

#if SLANG_WINDOWS_FAMILY
#define VK_USE_PLATFORM_WIN32_KHR 1
#elif SLANG_APPLE_FAMILY
#define VK_USE_PLATFORM_METAL_EXT 1
#elif SLANG_LINUX_FAMILY
#define VK_USE_PLATFORM_XLIB_KHR 1
#endif

#define VK_NO_PROTOTYPES

#include <vulkan/vulkan.h>

// Undef xlib macros
#ifdef Always
#undef Always
#endif
#ifdef None
#undef None
#endif

namespace rhi::vk {

struct VulkanModule
{
    /// true if has been initialized
    SLANG_FORCE_INLINE bool isInitialized() const { return m_module != nullptr; }

    /// Get a function by name
    PFN_vkVoidFunction getFunction(const char* name) const;

    /// true if using a software Vulkan implementation.
    bool isSoftware() const { return m_isSoftware; }

    /// Initialize
    Result init(bool useSoftwareImpl);
    /// Destroy
    void destroy();

    /// Dtor
    ~VulkanModule() { destroy(); }

protected:
    void* m_module = nullptr;
    bool m_isSoftware = false;
};

} // namespace rhi::vk
