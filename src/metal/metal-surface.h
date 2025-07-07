#pragma once

#include "metal-base.h"

namespace rhi::metal {

class SurfaceImpl : public Surface
{
public:
    RefPtr<DeviceImpl> m_device;
    WindowHandle m_windowHandle;
    NS::SharedPtr<CA::MetalLayer> m_metalLayer;
    NS::SharedPtr<CA::MetalDrawable> m_currentDrawable;

public:
    ~SurfaceImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL configure(const SurfaceConfig& config) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unconfigure() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL acquireNextImage(ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL present() override;
};

} // namespace rhi::metal
