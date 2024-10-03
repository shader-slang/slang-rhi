#pragma once

#include "vk-base.h"

#include "core/short_vector.h"

#include <vector>

namespace rhi::vk {

class SurfaceImpl : public Surface
{
public:
    VkSwapchainKHR m_swapChain;
    VkSurfaceKHR m_surface;
    /// Semaphore to signal after `acquireNextImage`.
    VkSemaphore m_nextImageSemaphore;
    VkFormat m_vkformat;
    // RefPtr<CommandQueueImpl> m_queue;
    short_vector<RefPtr<TextureImpl>> m_images;
    RefPtr<DeviceImpl> m_device;
    VulkanApi* m_api;
    WindowHandle m_windowHandle;
#if SLANG_APPLE_FAMILY
    void* m_metalLayer;
#endif

public:
    ~SurfaceImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL configure(const SurfaceConfig& config) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getCurrentTexture(ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL present() override;
};

} // namespace rhi::vk
