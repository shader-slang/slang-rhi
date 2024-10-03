#include "wgpu-surface.h"
#include "wgpu-device.h"

namespace rhi::wgpu {

SurfaceImpl::~SurfaceImpl()
{
    if (m_surface)
    {
        m_device->m_ctx.api.wgpuSurfaceRelease(m_surface);
    }
}

Result SurfaceImpl::configure(const SurfaceConfig& config)
{
    setConfig(config);

    WGPUSurfaceConfiguration wgpuConfig = {};
    wgpuConfig.device = m_device->m_ctx.device;
    wgpuConfig.format = WGPUTextureFormat_RGBA8Unorm;     // TODO
    wgpuConfig.usage = WGPUTextureUsage_RenderAttachment; // TODO
    wgpuConfig.viewFormatCount = 1;                       // TODO
    wgpuConfig.viewFormats = &wgpuConfig.format;          // TODO
    wgpuConfig.alphaMode = WGPUCompositeAlphaMode_Opaque;
    wgpuConfig.width = config.width;
    wgpuConfig.height = config.height;
    wgpuConfig.presentMode = WGPUPresentMode_Fifo; // TODO
    m_device->m_ctx.api.wgpuSurfaceConfigure(m_surface, &wgpuConfig);

    return SLANG_OK;
}

Result SurfaceImpl::getCurrentTexture(ITexture** outTexture)
{
    WGPUSurfaceTexture surfaceTexture;

    // WGPUTexture texture;
    // WGPUBool suboptimal;
    WGPUSurfaceGetCurrentTextureStatus status;

    m_device->m_ctx.api.wgpuSurfaceGetCurrentTexture(m_surface, &surfaceTexture);
    return SLANG_E_NOT_IMPLEMENTED;
}

Result SurfaceImpl::present()
{
    m_device->m_ctx.api.wgpuSurfacePresent(m_surface);
    return SLANG_OK;
}

Result DeviceImpl::createSurface(WindowHandle windowHandle, ISurface** outSurface)
{
    RefPtr<SurfaceImpl> surface = new SurfaceImpl();

    WGPUSurfaceDescriptor desc = {};
    WGPUSurfaceSourceWindowsHWND descHWD = {};
    WGPUSurfaceSourceXlibWindow descXlib = {};
    WGPUSurfaceSourceMetalLayer descMetal = {};

    switch (windowHandle.type)
    {
    case WindowHandle::Type::Win32Handle:
        descHWD.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
        descHWD.hinstance = 0;
        descHWD.hwnd = (void*)windowHandle.handleValues[0];
        desc.nextInChain = (WGPUChainedStruct*)&descHWD;
        break;
    case WindowHandle::Type::NSWindowHandle:
        descMetal.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
        // TODO
        descMetal.layer = (void*)windowHandle.handleValues[0];
        break;
    case WindowHandle::Type::XLibHandle:
        descXlib.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
        descXlib.display = (void*)windowHandle.handleValues[0];
        descXlib.window = (uint64_t)windowHandle.handleValues[1];
        break;
    }

    surface->m_surface = m_ctx.api.wgpuInstanceCreateSurface(m_ctx.instance, &desc);

    // Query capabilities
    WGPUSurfaceCapabilities capabilities;
    m_ctx.api.wgpuSurfaceGetCapabilities(surface->m_surface, m_ctx.adapter, &capabilities);

    returnComPtr(outSurface, surface);
    return SLANG_OK;
}

} // namespace rhi::wgpu
