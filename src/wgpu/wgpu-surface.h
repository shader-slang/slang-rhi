#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class SurfaceImpl : public Surface
{
public:
    RefPtr<DeviceImpl> m_device;
    WindowHandle m_windowHandle;
    std::vector<Format> m_supportedFormats;
    void* m_metalLayer = nullptr;
    WGPUSurface m_surface = nullptr;
    WGPUPresentMode m_vsyncOffMode = WGPUPresentMode(0);
    WGPUPresentMode m_vsyncOnMode = WGPUPresentMode(0);

    ~SurfaceImpl();

    Result init(DeviceImpl* device, WindowHandle windowHandle);

    virtual SLANG_NO_THROW Result SLANG_MCALL configure(const SurfaceConfig& config) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unconfigure() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL acquireNextImage(ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL present() override;
};

} // namespace rhi::wgpu
