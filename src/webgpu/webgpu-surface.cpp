#include "webgpu-surface.h"
#include "webgpu-device.h"
#include "webgpu-texture.h"
#include "webgpu-utils.h"

#include "../core/reverse-map.h"
#if SLANG_APPLE_FAMILY
#include "../cocoa-util.h"
#endif


namespace rhi::webgpu {

static auto translateWebGPUFormat =
    reverseMap<Format, WebGPUTextureFormat>(translateTextureFormat, Format::Undefined, Format::_Count);

SurfaceImpl::~SurfaceImpl()
{
    if (m_surface)
    {
        m_device->m_ctx.api.webgpuSurfaceRelease(m_surface);
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

    WebGPUSurfaceDescriptor desc = {};
#if SLANG_WINDOWS_FAMILY
    WebGPUSurfaceSourceWindowsHWND descHWD = {};
#elif SLANG_LINUX_FAMILY
    WebGPUSurfaceSourceXlibWindow descXlib = {};
#elif SLANG_APPLE_FAMILY
    WebGPUSurfaceSourceMetalLayer descMetal = {};
#endif

    switch (windowHandle.type)
    {
#if SLANG_WINDOWS_FAMILY
    case WindowHandleType::HWND:
        descHWD.chain.sType = WebGPUSType_SurfaceSourceWindowsHWND;
        descHWD.hinstance = 0;
        descHWD.hwnd = (void*)windowHandle.handleValues[0];
        desc.nextInChain = (WebGPUChainedStruct*)&descHWD;
        break;
#elif SLANG_APPLE_FAMILY
    case WindowHandleType::NSWindow:
        m_metalLayer = CocoaUtil::createMetalLayer((void*)windowHandle.handleValues[0]);
        descMetal.chain.sType = WebGPUSType_SurfaceSourceMetalLayer;
        descMetal.layer = m_metalLayer;
        desc.nextInChain = (WebGPUChainedStruct*)&descMetal;
        break;
#elif SLANG_LINUX_FAMILY
    case WindowHandleType::XlibWindow:
        descXlib.chain.sType = WebGPUSType_SurfaceSourceXlibWindow;
        descXlib.display = (void*)windowHandle.handleValues[0];
        descXlib.window = (uint64_t)windowHandle.handleValues[1];
        desc.nextInChain = (WebGPUChainedStruct*)&descXlib;
        break;
#endif
    default:
        return SLANG_E_INVALID_HANDLE;
    }

    m_surface = m_device->m_ctx.api.webgpuInstanceCreateSurface(m_device->m_ctx.instance, &desc);

    // Query capabilities
    WebGPUSurfaceCapabilities capabilities;
    m_device->m_ctx.api.webgpuSurfaceGetCapabilities(m_surface, m_device->m_ctx.adapter, &capabilities);

    // Get supported formats
    Format preferredFormat = Format::Undefined;
    for (size_t i = 0; i < capabilities.formatCount; i++)
    {
        Format format = translateWebGPUFormat(capabilities.formats[i]);
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
    if (capabilities.usages & WebGPUTextureUsage_CopySrc)
        usage |= TextureUsage::CopySource;
    if (capabilities.usages & WebGPUTextureUsage_CopyDst)
        usage |= TextureUsage::CopyDestination;
    if (capabilities.usages & WebGPUTextureUsage_TextureBinding)
        usage |= TextureUsage::ShaderResource;
    if (capabilities.usages & WebGPUTextureUsage_StorageBinding)
        usage |= TextureUsage::UnorderedAccess;
    if (capabilities.usages & WebGPUTextureUsage_RenderAttachment)
        usage |= TextureUsage::RenderTarget;

    m_info.preferredFormat = preferredFormat;
    m_info.formats = m_supportedFormats.data();
    m_info.formatCount = (uint32_t)m_supportedFormats.size();
    m_info.supportedUsage = usage;

    auto findPresentMode = [&](const WebGPUPresentMode* modes, size_t modeCount) -> WebGPUPresentMode
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
        return WebGPUPresentMode(0);
    };

    // Choose present modes.
    static const WebGPUPresentMode kVsyncOffModes[] = {
        WebGPUPresentMode_Immediate,
        WebGPUPresentMode_Mailbox,
        WebGPUPresentMode_Fifo,
    };
    static const WebGPUPresentMode kVsyncOnModes[] = {
        WebGPUPresentMode_FifoRelaxed,
        WebGPUPresentMode_Fifo,
        WebGPUPresentMode_Immediate,
        WebGPUPresentMode_Mailbox,
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

    WebGPUSurfaceConfiguration webgpuConfig = {};
    webgpuConfig.device = m_device->m_ctx.device;
    webgpuConfig.format = translateTextureFormat(m_config.format);
    webgpuConfig.usage = translateTextureUsage(usage);
    // TODO: support more view formats
    webgpuConfig.viewFormatCount = 1;
    webgpuConfig.viewFormats = &webgpuConfig.format;
    webgpuConfig.alphaMode = WebGPUCompositeAlphaMode_Opaque;
    webgpuConfig.width = m_config.width;
    webgpuConfig.height = m_config.height;
    webgpuConfig.presentMode = m_config.vsync ? m_vsyncOnMode : m_vsyncOffMode;
    m_device->m_ctx.api.webgpuSurfaceConfigure(m_surface, &webgpuConfig);
    m_configured = true;

    return SLANG_OK;
}

Result SurfaceImpl::unconfigure()
{
    if (!m_configured)
    {
        return SLANG_OK;
    }

    m_device->m_ctx.api.webgpuSurfaceUnconfigure(m_surface);
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

    WebGPUSurfaceTexture surfaceTexture;
    m_device->m_ctx.api.webgpuSurfaceGetCurrentTexture(m_surface, &surfaceTexture);
    if (surfaceTexture.status != WebGPUSurfaceGetCurrentTextureStatus_Success)
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
    m_device->m_ctx.api.webgpuSurfacePresent(m_surface);
    return SLANG_OK;
}

Result DeviceImpl::createSurface(WindowHandle windowHandle, ISurface** outSurface)
{
    RefPtr<SurfaceImpl> surface = new SurfaceImpl();
    SLANG_RETURN_ON_FAIL(surface->init(this, windowHandle));
    returnComPtr(outSurface, surface);
    return SLANG_OK;
}

} // namespace rhi::webgpu
