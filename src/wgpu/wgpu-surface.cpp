#include "wgpu-surface.h"
#include "wgpu-device.h"
#include "wgpu-texture.h"
#include "wgpu-utils.h"

#include "../core/reverse-map.h"
#if SLANG_APPLE_FAMILY
#include "../cocoa-util.h"
#endif


namespace rhi::wgpu {

static Format translateWGPUFormat(WGPUTextureFormat format)
{
    return reverseMapLookup<Format, WGPUTextureFormat, Format::Undefined, Format::_Count>(
        translateTextureFormat,
        format
    );
}

SurfaceImpl::~SurfaceImpl()
{
    // Release an unused reservation before its native surface is destroyed.
    m_currentTexture = nullptr;
    if (m_surface)
    {
        m_device->m_ctx.api.wgpuSurfaceRelease(m_surface);
    }
#if SLANG_APPLE_FAMILY
    if (m_metalLayer)
    {
        CocoaUtil::destroyMetalLayer(m_metalLayer);
    }
#endif
}

Result SurfaceImpl::init(DeviceImpl* device, WindowHandle windowHandle)
{
    m_device = device;
    m_windowHandle = windowHandle;

    WGPUSurfaceDescriptor desc = {};
#if SLANG_WINDOWS_FAMILY
    WGPUSurfaceSourceWindowsHWND descHWD = {};
#elif SLANG_LINUX_FAMILY
    WGPUSurfaceSourceXlibWindow descXlib = {};
#elif SLANG_APPLE_FAMILY
    WGPUSurfaceSourceMetalLayer descMetal = {};
#elif SLANG_WASM
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector descCanvas = {};
#endif

    switch (windowHandle.type)
    {
#if SLANG_WINDOWS_FAMILY
    case WindowHandleType::HWND:
        descHWD.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
        descHWD.hinstance = 0;
        descHWD.hwnd = (void*)windowHandle.handleValues[0];
        desc.nextInChain = (WGPUChainedStruct*)&descHWD;
        break;
#elif SLANG_APPLE_FAMILY
    case WindowHandleType::NSWindow:
        m_metalLayer = CocoaUtil::createMetalLayer((void*)windowHandle.handleValues[0]);
        descMetal.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
        descMetal.layer = m_metalLayer;
        desc.nextInChain = (WGPUChainedStruct*)&descMetal;
        break;
#elif SLANG_LINUX_FAMILY
    case WindowHandleType::XlibWindow:
        descXlib.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
        descXlib.display = (void*)windowHandle.handleValues[0];
        descXlib.window = (uint64_t)windowHandle.handleValues[1];
        desc.nextInChain = (WGPUChainedStruct*)&descXlib;
        break;
#elif SLANG_WASM
    case WindowHandleType::WGPUCanvas:
        descCanvas.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
        {
            const char* selector = windowHandle.canvasSelector;
            descCanvas.selector = {selector, strlen(selector)};
        }
        desc.nextInChain = (WGPUChainedStruct*)&descCanvas;
        break;
#endif
    default:
        return SLANG_E_INVALID_HANDLE;
    }

    m_surface = m_device->m_ctx.api.wgpuInstanceCreateSurface(m_device->m_ctx.instance, &desc);

    // Query capabilities
    WGPUSurfaceCapabilities capabilities = {};
    m_device->m_ctx.api.wgpuSurfaceGetCapabilities(m_surface, m_device->m_ctx.adapter, &capabilities);

    // Get supported formats
    Format preferredFormat = Format::Undefined;
    for (size_t i = 0; i < capabilities.formatCount; i++)
    {
        Format format = translateWGPUFormat(capabilities.formats[i]);
        if (format != Format::Undefined)
            m_supportedFormats.push_back(format);
        if (format == Format::BGRA8UnormSrgb)
            preferredFormat = format;
    }
    if (preferredFormat == Format::Undefined && !m_supportedFormats.empty())
    {
        preferredFormat = m_supportedFormats[0];
    }

    // Get supported usage
    TextureUsage usage = TextureUsage::None;
    if (capabilities.usages & WGPUTextureUsage_CopySrc)
        usage |= TextureUsage::CopySource;
    if (capabilities.usages & WGPUTextureUsage_CopyDst)
        usage |= TextureUsage::CopyDestination;
    if (capabilities.usages & WGPUTextureUsage_TextureBinding)
        usage |= TextureUsage::ShaderResource;
    if (capabilities.usages & WGPUTextureUsage_StorageBinding)
        usage |= TextureUsage::UnorderedAccess;
    if (capabilities.usages & WGPUTextureUsage_RenderAttachment)
        usage |= TextureUsage::RenderTarget;

#if SLANG_WASM
    // wgpuSurfaceGetCapabilities might return 0 usages if the adapter was created
    // without a compatible surface. Force RenderTarget as it's required for swapchains.
    if (usage == TextureUsage::None)
        usage = TextureUsage::RenderTarget;
#endif

    m_info.preferredFormat = preferredFormat;
    m_info.formats = m_supportedFormats.data();
    m_info.formatCount = (uint32_t)m_supportedFormats.size();
    m_info.supportedUsage = TextureUsage::Present | usage;

    auto findPresentMode = [&](const WGPUPresentMode* modes, size_t modeCount) -> WGPUPresentMode
    {
        for (size_t i = 0; i < modeCount; ++i)
        {
            for (size_t j = 0; j < capabilities.presentModeCount; ++j)
            {
                if (modes[i] == capabilities.presentModes[j])
                {
                    return modes[i];
                }
            }
        }
        return WGPUPresentMode(0);
    };

    // Choose present modes.
    static const WGPUPresentMode kVsyncOffModes[] = {
        WGPUPresentMode_Immediate,
        WGPUPresentMode_Mailbox,
        WGPUPresentMode_Fifo,
    };
    static const WGPUPresentMode kVsyncOnModes[] = {
        WGPUPresentMode_Fifo,
        WGPUPresentMode_FifoRelaxed,
        WGPUPresentMode_Immediate,
        WGPUPresentMode_Mailbox,
    };
    m_vsyncOffMode = findPresentMode(kVsyncOffModes, SLANG_COUNT_OF(kVsyncOffModes));
    m_vsyncOnMode = findPresentMode(kVsyncOnModes, SLANG_COUNT_OF(kVsyncOnModes));

    return SLANG_OK;
}

Result SurfaceImpl::configure(const SurfaceConfig& config)
{
    SLANG_RETURN_ON_FAIL(validateConfig(config));
    SurfaceConfig resolvedConfig = config;
    if (resolvedConfig.format == Format::Undefined)
    {
        resolvedConfig.format = m_info.preferredFormat;
    }
    if (resolvedConfig.usage == TextureUsage::None)
    {
        resolvedConfig.usage = (TextureUsage::Present | TextureUsage::RenderTarget | TextureUsage::CopyDestination) &
                               m_info.supportedUsage;
    }

    if (is_set(resolvedConfig.usage, TextureUsage::UnorderedAccess))
    {
        FormatSupport formatSupport = FormatSupport::None;
        SLANG_RETURN_ON_FAIL(m_device->getFormatSupport(resolvedConfig.format, &formatSupport));
        if (!is_set(formatSupport, FormatSupport::ShaderUavStore))
            return SLANG_E_INVALID_ARG;
    }

    WGPUTextureUsage usage = translateTextureUsage(resolvedConfig.usage);
    // Present is an RHI surface semantic, while WebGPU requires a concrete native usage.
    // Keep RenderAttachment as an internal implementation detail for Present-only configs.
    if (usage == WGPUTextureUsage_None)
        usage = WGPUTextureUsage_RenderAttachment;

    WGPUSurfaceConfiguration wgpuConfig = {};
    wgpuConfig.device = m_device->m_ctx.device;
    wgpuConfig.format = translateTextureFormat(resolvedConfig.format);
    wgpuConfig.usage = usage;
    // TODO: support more view formats
    wgpuConfig.viewFormatCount = 1;
    wgpuConfig.viewFormats = &wgpuConfig.format;
    wgpuConfig.alphaMode = WGPUCompositeAlphaMode_Opaque;
    wgpuConfig.width = resolvedConfig.width;
    wgpuConfig.height = resolvedConfig.height;
    wgpuConfig.presentMode = resolvedConfig.vsync ? m_vsyncOnMode : m_vsyncOffMode;
    if (m_device->getAndClearLastUncapturedError() != WGPUErrorType_NoError)
        m_device->printWarning("WebGPU device had reported an error before surface configuration.");

    // Reconfiguration invalidates the old native image, including an unexposed reservation.
    m_currentTexture = nullptr;
    m_imageAcquired = false;
    m_device->m_ctx.api.wgpuSurfaceConfigure(m_surface, &wgpuConfig);
    if (m_device->getAndClearLastUncapturedError() != WGPUErrorType_NoError)
        return SLANG_FAIL;

    setConfig(resolvedConfig);
    m_configured = true;

    return SLANG_OK;
}

Result SurfaceImpl::unconfigure()
{
    // Dropping a reserved image needs no artificial presentation.
    m_currentTexture = nullptr;
    m_imageAcquired = false;
    // An outdated acquisition can invalidate the public configuration while native surface
    // resources still exist, so unconfigure them even when getConfig() already returns null.
    m_device->m_ctx.api.wgpuSurfaceUnconfigure(m_surface);
    m_configured = false;

    return SLANG_OK;
}

Result SurfaceImpl::waitForNextImage()
{
#if SLANG_WASM
    // Browser canvas images expire at event-loop boundaries. The browser must schedule the
    // frame (for example with requestAnimationFrame); a blocking native wait cannot do that.
    return SLANG_E_NOT_IMPLEMENTED;
#else
    // Native WebGPU performs its acquisition work here. It does not expose an exact display
    // deadline or a separate readiness fence, so this only moves the backend's acquisition wait.
    return prepareNextImage();
#endif
}

Result SurfaceImpl::prepareNextImage()
{
    if (!m_configured || m_imageAcquired)
    {
        return SLANG_FAIL;
    }
    if (m_currentTexture)
        return SLANG_OK;

    WGPUSurfaceTexture surfaceTexture = {};
    m_device->m_ctx.api.wgpuSurfaceGetCurrentTexture(m_surface, &surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
    {
        // Some implementations return an owned texture even for an unsuccessful acquisition.
        if (surfaceTexture.texture)
            m_device->m_ctx.api.wgpuTextureRelease(surfaceTexture.texture);
        // Timeouts can be retried, while an outdated image needs a new swapchain configuration.
        // Surface loss and device errors remain failures because configuration alone cannot
        // promise to recover a lost native surface.
        if (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Timeout)
            return SLANG_E_NOT_AVAILABLE;
        if (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Outdated)
        {
            m_configured = false;
            return SLANG_E_NOT_AVAILABLE;
        }
        return SLANG_FAIL;
    }

    TextureDesc textureDesc = {};
    textureDesc.type = TextureType::Texture2D;
    textureDesc.size.width = m_config.width;
    textureDesc.size.height = m_config.height;
    textureDesc.size.depth = 1;
    textureDesc.arrayLength = 1;
    textureDesc.mipCount = 1;
    textureDesc.format = m_config.format;
    textureDesc.usage = m_config.usage;
    textureDesc.defaultState = ResourceState::Present;

    RefPtr<TextureImpl> texture = new TextureImpl(m_device, textureDesc);
    texture->m_texture = surfaceTexture.texture;
    m_currentTexture = texture;
    return SLANG_OK;
}

Result SurfaceImpl::acquireNextImage(ITexture** outTexture)
{
    *outTexture = nullptr;
    // Acquire without wait remains available on every platform, including browser frame callbacks.
    SLANG_RETURN_ON_FAIL(prepareNextImage());
    m_imageAcquired = true;
    returnComPtrCopy(outTexture, m_currentTexture);
    return SLANG_OK;
}

Result SurfaceImpl::present()
{
    if (!m_configured || !m_imageAcquired)
    {
        return SLANG_FAIL;
    }
#if !SLANG_WASM
    m_device->m_ctx.api.wgpuSurfacePresent(m_surface);
#endif
    // The next wait/acquire must obtain the next native image rather than reuse this frame.
    m_currentTexture = nullptr;
    m_imageAcquired = false;
    return SLANG_OK;
}

Result DeviceImpl::createSurface(WindowHandle windowHandle, ISurface** outSurface)
{
    RefPtr<SurfaceImpl> surface = new SurfaceImpl();
    SLANG_RETURN_ON_FAIL(surface->init(this, windowHandle));
    returnComPtr(outSurface, surface);
    return SLANG_OK;
}

} // namespace rhi::wgpu
