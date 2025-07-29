#include "metal-surface.h"
#include "metal-device.h"
#include "metal-texture.h"
#include "metal-utils.h"
#include "../cocoa-util.h"

namespace rhi::metal {

// Supported pixel formats
// https://developer.apple.com/documentation/quartzcore/cametallayer/1478155-pixelformat
static const Format kSupportedFormats[] = {
    Format::BGRA8Unorm,
    Format::BGRA8UnormSrgb,
    Format::RGBA16Float,
    Format::RGB10A2Unorm,
    // Additional formats
    // MTLPixelFormat.bgr10a2Unorm (macOS only)
    // MTLPixelFormat.bgra10_xr
    // MTLPixelFormat.bgra10_xr_srgb
    // MTLPixelFormat.bgr10_xr
    // MTLPixelFormat.bgr10_xr_srgb
};

SurfaceImpl::~SurfaceImpl() {}

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
        // TODO: Once we have propert support for format support, we can add additional usages depending on the format.
        m_config.usage = TextureUsage::Present | TextureUsage::RenderTarget | TextureUsage::CopyDestination;
    }

    m_metalLayer->setPixelFormat(translatePixelFormat(m_config.format));
    m_metalLayer->setDrawableSize(CGSize{(float)m_config.width, (float)m_config.height});
    m_metalLayer->setFramebufferOnly(m_config.usage == TextureUsage::RenderTarget);
    // m_metalLayer->setDisplaySyncEnabled(config.vsync);
    m_configured = true;

    return SLANG_OK;
}

Result SurfaceImpl::unconfigure()
{
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

    m_currentDrawable = NS::RetainPtr(m_metalLayer->nextDrawable());
    if (!m_currentDrawable)
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
    texture->m_texture = NS::RetainPtr(m_currentDrawable->texture());
    texture->m_textureType = texture->m_texture->textureType();
    texture->m_pixelFormat = texture->m_texture->pixelFormat();

    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

Result SurfaceImpl::present()
{
    if (!m_currentDrawable)
    {
        return SLANG_FAIL;
    }

    MTL::CommandBuffer* commandBuffer = m_device->m_commandQueue->commandBuffer();
    commandBuffer->presentDrawable(m_currentDrawable.get());
    commandBuffer->commit();
    commandBuffer->release();
    m_currentDrawable.reset();

    return SLANG_OK;
}

Result DeviceImpl::createSurface(WindowHandle windowHandle, ISurface** outSurface)
{
    RefPtr<SurfaceImpl> surface = new SurfaceImpl();

    surface->m_device = this;
    surface->m_windowHandle = windowHandle;
    surface->m_metalLayer =
        NS::TransferPtr((CA::MetalLayer*)CocoaUtil::createMetalLayer((void*)windowHandle.handleValues[0]));
    if (!surface->m_metalLayer)
    {
        return SLANG_FAIL;
    }
    surface->m_metalLayer->setDevice(m_device.get());

    surface->m_info.preferredFormat = Format::BGRA8UnormSrgb;
    surface->m_info.supportedUsage = TextureUsage::Present | TextureUsage::RenderTarget | TextureUsage::ShaderResource |
                                     TextureUsage::UnorderedAccess | TextureUsage::CopyDestination;
    surface->m_info.formats = kSupportedFormats;
    surface->m_info.formatCount = SLANG_COUNT_OF(kSupportedFormats);

    returnComPtr(outSurface, surface);
    return SLANG_OK;
}

} // namespace rhi::metal
