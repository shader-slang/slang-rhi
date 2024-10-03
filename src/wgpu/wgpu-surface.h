#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class SurfaceImpl : public Surface
{
public:
    RefPtr<DeviceImpl> m_device;
    WGPUSurface m_surface = nullptr;

    ~SurfaceImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL configure(const SurfaceConfig& config) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getCurrentTexture(ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL present() override;
};

} // namespace rhi::wgpu
