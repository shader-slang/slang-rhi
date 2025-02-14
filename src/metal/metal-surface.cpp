#include "metal-surface.h"
#include "metal-device.h"
#include "metal-texture.h"
#include "metal-util.h"
#include "../cocoa-util.h"

namespace rhi::metal {

// Supported pixel formats
// https://developer.apple.com/documentation/quartzcore/cametallayer/1478155-pixelformat
static const Format kSupportedFormats[] = {
    Format::B8G8R8A8_UNORM,
    Format::B8G8R8A8_UNORM_SRGB,
    Format::R16G16B16A16_FLOAT,
    Format::R10G10B10A2_UNORM,
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

    if (config.width == 0 || config.height == 0)
    {
        return SLANG_FAIL;
    }

    Format format = config.format == Format::Unknown ? m_info.preferredFormat : config.format;
    m_metalLayer->setPixelFormat(MetalUtil::translatePixelFormat(format));
    m_metalLayer->setDrawableSize(CGSize{(float)config.width, (float)config.height});
    m_metalLayer->setFramebufferOnly(config.usage == TextureUsage::RenderTarget);
    // m_metalLayer->setDisplaySyncEnabled(config.vsync);

    return SLANG_OK;
}

Result SurfaceImpl::getCurrentTexture(ITexture** outTexture)
{
    m_currentDrawable = NS::RetainPtr(m_metalLayer->nextDrawable());
    if (!m_currentDrawable)
    {
        return SLANG_FAIL;
    }

    TextureDesc textureDesc = {};
    textureDesc.usage = m_config.usage;
    textureDesc.type = TextureType::Texture2D;
    textureDesc.arrayLength = 1;
    textureDesc.format = m_config.format;
    textureDesc.size.width = m_config.width;
    textureDesc.size.height = m_config.height;
    textureDesc.size.depth = 1;
    textureDesc.mipLevelCount = 1;
    textureDesc.defaultState = ResourceState::Present;
    RefPtr<TextureImpl> texture = new TextureImpl(textureDesc);
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

    surface->m_info.preferredFormat = Format::B8G8R8A8_UNORM;
    surface->m_info.supportedUsage = TextureUsage::Present | TextureUsage::RenderTarget | TextureUsage::ShaderResource |
                                     TextureUsage::UnorderedAccess | TextureUsage::CopyDestination;
    surface->m_info.formats = kSupportedFormats;
    surface->m_info.formatCount = SLANG_COUNT_OF(kSupportedFormats);

    returnComPtr(outSurface, surface);
    return SLANG_OK;
}

} // namespace rhi::metal
