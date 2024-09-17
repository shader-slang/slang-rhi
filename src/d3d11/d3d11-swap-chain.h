#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class SwapchainImpl : public D3DSwapchainBase
{
public:
    RefPtr<DeviceImpl> m_device;
    ComPtr<ID3D11Device> m_d3dDevice;
    ComPtr<IDXGIFactory> m_dxgiFactory;
    Result init(DeviceImpl* device, const ISwapchain::Desc& swapchainDesc, WindowHandle window);

    virtual void createSwapchainBufferImages() override;
    virtual IDXGIFactory* getDXGIFactory() override { return m_dxgiFactory; }
    virtual IUnknown* getOwningDevice() override { return m_d3dDevice; }
    virtual SLANG_NO_THROW Result SLANG_MCALL resize(GfxCount width, GfxCount height) override;
    virtual SLANG_NO_THROW bool SLANG_MCALL isOccluded() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setFullScreenMode(bool mode) override;
};

} // namespace rhi::d3d11
