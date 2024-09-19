#include "d3d11-swap-chain.h"
#include "d3d11-device.h"
#include "d3d11-texture.h"

namespace rhi::d3d11 {

Result SwapchainImpl::init(DeviceImpl* device, const ISwapchain::Desc& swapchainDesc, WindowHandle window)
{
    m_device = device;
    m_d3dDevice = device->m_device;
    m_dxgiFactory = device->m_dxgiFactory;
    return D3DSwapchainBase::init(swapchainDesc, window, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);
}

void SwapchainImpl::createSwapchainBufferImages()
{
    m_images.clear();
    // D3D11 implements automatic back buffer rotation, so the application
    // always render to buffer 0.
    ComPtr<ID3D11Resource> d3dResource;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(d3dResource.writeRef()));
    TextureDesc imageDesc = {};
    imageDesc.type = TextureType::Texture2D;
    imageDesc.arrayLength = 1;
    imageDesc.numMipLevels = 1;
    imageDesc.size.width = m_desc.width;
    imageDesc.size.height = m_desc.height;
    imageDesc.size.depth = 1;
    imageDesc.format = m_desc.format;
    imageDesc.usage = TextureUsage::Present | TextureUsage::CopyDestination | TextureUsage::RenderTarget;
    imageDesc.defaultState = ResourceState::Present;
    RefPtr<TextureImpl> image = new TextureImpl(m_device, imageDesc);
    image->m_resource = d3dResource;
    for (GfxIndex i = 0; i < m_desc.imageCount; i++)
    {
        m_images.push_back(image);
    }
}

Result SwapchainImpl::resize(GfxCount width, GfxCount height)
{
    m_device->m_immediateContext->ClearState();
    return D3DSwapchainBase::resize(width, height);
}

bool SwapchainImpl::isOccluded()
{
    return false;
}

Result SwapchainImpl::setFullScreenMode(bool mode)
{
    return SLANG_FAIL;
}

} // namespace rhi::d3d11
