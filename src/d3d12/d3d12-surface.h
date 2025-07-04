#pragma once

#include "d3d12-base.h"
#include "../d3d/d3d-surface.h"

namespace rhi::d3d12 {

class SurfaceImpl : public D3DSurface
{
public:
    RefPtr<DeviceImpl> m_device;
    ComPtr<ID3D12CommandQueue> m_queue;
    ComPtr<IDXGIFactory> m_dxgiFactory;
    ComPtr<IDXGISwapChain3> m_swapChain3;
    ComPtr<ID3D12Fence> m_fence;
    short_vector<HANDLE> m_frameEvents;
    uint64_t fenceValue = 0;

    Result init(DeviceImpl* device, WindowHandle windowHandle);

    virtual void createSwapchainTextures(uint32_t count) override;
    virtual IDXGIFactory* getDXGIFactory() override { return m_dxgiFactory; }
    virtual IUnknown* getOwningDevice() override { return m_queue; }

    virtual SLANG_NO_THROW Result SLANG_MCALL configure(const SurfaceConfig& config) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unconfigure() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL acquireNextImage(ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL present() override;
};

} // namespace rhi::d3d12
