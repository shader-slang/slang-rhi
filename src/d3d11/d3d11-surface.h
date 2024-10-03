#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class SurfaceImpl : public D3DSurface
{
public:
    RefPtr<DeviceImpl> m_device;
    ComPtr<ID3D11Device> m_d3dDevice;
    ComPtr<IDXGIFactory> m_dxgiFactory;

    Result init(DeviceImpl* device, WindowHandle windowHandle);

    virtual void createSwapchainTextures(uint32_t count) override;
    virtual IDXGIFactory* getDXGIFactory() override { return m_dxgiFactory; }
    virtual IUnknown* getOwningDevice() override { return m_d3dDevice; }

    virtual SLANG_NO_THROW Result SLANG_MCALL configure(const SurfaceConfig& config) override;
    // virtual SLANG_NO_THROW Result SLANG_MCALL getCurrentTexture(ITexture** outTexture) override;
    // virtual SLANG_NO_THROW Result SLANG_MCALL present() override;
};

} // namespace rhi::d3d11
