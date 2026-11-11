#pragma once

#include "vk-base.h"

#include "core/short_vector.h"

#include <vector>

namespace rhi::vk {

class SurfaceImpl : public Surface
{
public:
    RefPtr<DeviceImpl> m_device;
    WindowHandle m_windowHandle;
    std::vector<Format> m_supportedFormats;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    short_vector<RefPtr<TextureImpl>> m_textures;

    struct FrameData
    {
        // Fence to signal when the rendering to the swapchain image is finished.
        VkFence fence;
        // Semaphore to signal when the swapchain image is available.
        VkSemaphore imageAvailableSemaphore;
        // Semaphore to signal when the rendering to the swapchain image is finished.
        VkSemaphore renderFinishedSemaphore;
    };
    short_vector<FrameData> m_frameData;

    uint32_t m_currentFrameIndex = 0;
    uint32_t m_currentTextureIndex = -1;
    // A reservation waits for image availability without arming the queue's next submission.
    VkFence m_acquisitionFence = VK_NULL_HANDLE;
    bool m_imageAcquired = false;
#if SLANG_APPLE_FAMILY
    void* m_metalLayer;
#endif

public:
    ~SurfaceImpl();

    Result init(DeviceImpl* device, WindowHandle windowHandle);
    Result createSwapchain();
    void destroySwapchain();
    Result reserveNextImage(bool waitForAvailability);

    virtual SLANG_NO_THROW Result SLANG_MCALL configure(const SurfaceConfig& config) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unconfigure() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL waitForNextImage() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL acquireNextImage(ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL present() override;
};

} // namespace rhi::vk
