#include "wgpu-surface.h"
#include "wgpu-device.h"
#include "wgpu-texture.h"
#include "wgpu-utils.h"

#include "../core/reverse-map.h"
#if SLANG_APPLE_FAMILY
#include "../cocoa-util.h"
#endif


namespace rhi::wgpu {

static auto translateWGPUFormat =
    reverseMap<Format, WGPUTextureFormat>(translateTextureFormat, Format::Undefined, Format::_Count);

SurfaceImpl::~SurfaceImpl()
{
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
#endif
    default:
        return SLANG_E_INVALID_HANDLE;
    }

    m_surface = m_device->m_ctx.api.wgpuInstanceCreateSurface(m_device->m_ctx.instance, &desc);

    // Query capabilities
    WGPUSurfaceCapabilities capabilities;
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

    m_info.preferredFormat = preferredFormat;
    m_info.formats = m_supportedFormats.data();
    m_info.formatCount = (uint32_t)m_supportedFormats.size();
    m_info.supportedUsage = usage;

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
        WGPUPresentMode_FifoRelaxed,
        WGPUPresentMode_Fifo,
        WGPUPresentMode_Immediate,
        WGPUPresentMode_Mailbox,
    };
    m_vsyncOffMode = findPresentMode(kVsyncOffModes, SLANG_COUNT_OF(kVsyncOffModes));
    m_vsyncOnMode = findPresentMode(kVsyncOnModes, SLANG_COUNT_OF(kVsyncOnModes));

    return SLANG_OK;
}

Result SurfaceImpl::configure(const SurfaceConfig& config)
{
    setConfig(config);

    if (m_config.width == 0 || m_config.height == 0)
    {
        return SLANG_FAIL;
    }
    if (m_config.format == Format::Undefined)
    {
        m_config.format = m_info.preferredFormat;
    }
    if (m_config.usage == TextureUsage::None)
    {
        m_config.usage = m_info.supportedUsage;
    }

    // sRGB formats cannot be used as storage textures.
    TextureUsage usage = m_config.usage;
    if (getFormatInfo(m_config.format).isSrgb)
    {
        usage &= ~TextureUsage::UnorderedAccess;
    }

    WGPUSurfaceConfiguration wgpuConfig = {};
    wgpuConfig.device = m_device->m_ctx.device;
    wgpuConfig.format = translateTextureFormat(m_config.format);
    wgpuConfig.usage = translateTextureUsage(usage);
    // TODO: support more view formats
    wgpuConfig.viewFormatCount = 1;
    wgpuConfig.viewFormats = &wgpuConfig.format;
    wgpuConfig.alphaMode = WGPUCompositeAlphaMode_Opaque;
    wgpuConfig.width = m_config.width;
    wgpuConfig.height = m_config.height;
    wgpuConfig.presentMode = m_config.vsync ? m_vsyncOnMode : m_vsyncOffMode;
    m_device->m_ctx.api.wgpuSurfaceConfigure(m_surface, &wgpuConfig);
    m_configured = true;

    return SLANG_OK;
}

Result SurfaceImpl::unconfigure()
{
    if (!m_configured)
    {
        return SLANG_OK;
    }

    m_device->m_ctx.api.wgpuSurfaceUnconfigure(m_surface);
    m_configured = false;

    return SLANG_OK;
}

Result SurfaceImpl::acquireNextImage(ITexture** outTexture)
{
    *outTexture = nullptr;

    if (!m_configured)
    {
        return SLANG_FAIL;
    }

    WGPUSurfaceTexture surfaceTexture;
    m_device->m_ctx.api.wgpuSurfaceGetCurrentTexture(m_surface, &surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success)
    {
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
    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

Result SurfaceImpl::present()
{
    if (!m_configured)
    {
        return SLANG_FAIL;
    }
    m_device->m_ctx.api.wgpuSurfacePresent(m_surface);
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
