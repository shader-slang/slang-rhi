#include "d3d11-surface.h"
#include "d3d11-device.h"
#include "d3d11-texture.h"

namespace rhi::d3d11 {

Result SurfaceImpl::init(DeviceImpl* device, WindowHandle windowHandle)
{
    m_device = device;
    m_d3dDevice = device->m_device;
    m_dxgiFactory = device->m_dxgiFactory;
    return D3DSurface::init(windowHandle, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, true);
}

void SurfaceImpl::createSwapchainTextures(uint32_t count)
{
    // D3D11 implements automatic back buffer rotation, so the application
    // always render to buffer 0.
    ComPtr<ID3D11Resource> d3dResource;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(d3dResource.writeRef()));
    TextureDesc textureDesc = {};
    textureDesc.type = TextureType::Texture2D;
    textureDesc.arrayLength = 1;
    textureDesc.mipLevelCount = 1;
    textureDesc.size.width = m_config.width;
    textureDesc.size.height = m_config.height;
    textureDesc.size.depth = 1;
    textureDesc.format = m_config.format;
    textureDesc.usage = m_config.usage;
    textureDesc.defaultState = ResourceState::Present;
    RefPtr<TextureImpl> texture = new TextureImpl(m_device, textureDesc);
    texture->m_resource = d3dResource;
    for (uint32_t i = 0; i < count; i++)
    {
        m_textures.push_back(texture);
    }
}

Result SurfaceImpl::configure(const SurfaceConfig& config)
{
    m_device->m_immediateContext->ClearState();
    return D3DSurface::configure(config);
}

Result DeviceImpl::createSurface(WindowHandle windowHandle, ISurface** outSurface)
{
    RefPtr<SurfaceImpl> surface = new SurfaceImpl();
    SLANG_RETURN_ON_FAIL(surface->init(this, windowHandle));
    returnComPtr(outSurface, surface);
    return SLANG_OK;
}

} // namespace rhi::d3d11
