#pragma once

#include "webgpu-base.h"

namespace rhi::webgpu {

class SurfaceImpl : public Surface
{
public:
    RefPtr<DeviceImpl> m_device;
    WindowHandle m_windowHandle;
    std::vector<Format> m_supportedFormats;
    void* m_metalLayer = nullptr;
    WebGPUSurface m_surface = nullptr;
    WebGPUPresentMode m_vsyncOffMode = WebGPUPresentMode(0);
    WebGPUPresentMode m_vsyncOnMode = WebGPUPresentMode(0);

    ~SurfaceImpl();

    Result init(DeviceImpl* device, WindowHandle windowHandle);

    virtual SLANG_NO_THROW Result SLANG_MCALL configure(const SurfaceConfig& config) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unconfigure() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL acquireNextImage(ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL present() override;
};

} // namespace rhi::webgpu
