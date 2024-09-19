#include "d3d12-swap-chain.h"
#include "d3d12-command-queue.h"
#include "d3d12-device.h"
#include "d3d12-texture.h"

namespace rhi::d3d12 {

Result SwapchainImpl::init(DeviceImpl* device, const ISwapchain::Desc& swapchainDesc, WindowHandle window)
{
    m_device = device;
    m_queue = static_cast<CommandQueueImpl*>(swapchainDesc.queue)->m_d3dQueue;
    m_dxgiFactory = device->m_deviceInfo.m_dxgiFactory;
    SLANG_RETURN_ON_FAIL(D3DSwapchainBase::init(swapchainDesc, window, DXGI_SWAP_EFFECT_FLIP_DISCARD));
    SLANG_RETURN_ON_FAIL(device->m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.writeRef())));

    SLANG_RETURN_ON_FAIL(m_swapChain->QueryInterface(m_swapChain3.writeRef()));
    for (GfxIndex i = 0; i < swapchainDesc.imageCount; i++)
    {
        m_frameEvents.push_back(
            CreateEventEx(nullptr, FALSE, CREATE_EVENT_INITIAL_SET | CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS)
        );
    }
    return SLANG_OK;
}

Result SwapchainImpl::resize(GfxCount width, GfxCount height)
{
    for (auto evt : m_frameEvents)
        SetEvent(evt);
    SLANG_RETURN_ON_FAIL(D3DSwapchainBase::resize(width, height));
    return SLANG_OK;
}

void SwapchainImpl::createSwapchainBufferImages()
{
    m_images.clear();

    for (GfxIndex i = 0; i < m_desc.imageCount; i++)
    {
        ComPtr<ID3D12Resource> d3dResource;
        m_swapChain->GetBuffer(i, IID_PPV_ARGS(d3dResource.writeRef()));
        TextureDesc imageDesc = {};
        imageDesc.usage = TextureUsage::Present | TextureUsage::RenderTarget | TextureUsage::CopyDestination;
        imageDesc.type = TextureType::Texture2D;
        imageDesc.arrayLength = 1;
        imageDesc.format = m_desc.format;
        imageDesc.size.width = m_desc.width;
        imageDesc.size.height = m_desc.height;
        imageDesc.size.depth = 1;
        imageDesc.numMipLevels = 1;
        imageDesc.defaultState = ResourceState::Present;
        RefPtr<TextureImpl> image = new TextureImpl(m_device, imageDesc);
        image->m_resource.setResource(d3dResource.get());
        image->m_defaultState = D3D12_RESOURCE_STATE_PRESENT;
        m_images.push_back(image);
    }
    for (auto evt : m_frameEvents)
        SetEvent(evt);
}

int SwapchainImpl::acquireNextImage()
{
    auto result = (int)m_swapChain3->GetCurrentBackBufferIndex();
    WaitForSingleObject(m_frameEvents[result], INFINITE);
    ResetEvent(m_frameEvents[result]);
    return result;
}

Result SwapchainImpl::present()
{
    m_fence->SetEventOnCompletion(fenceValue, m_frameEvents[m_swapChain3->GetCurrentBackBufferIndex()]);
    SLANG_RETURN_ON_FAIL(D3DSwapchainBase::present());
    fenceValue++;
    m_queue->Signal(m_fence, fenceValue);
    return SLANG_OK;
}

bool SwapchainImpl::isOccluded()
{
    return (m_swapChain3->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED);
}

Result SwapchainImpl::setFullScreenMode(bool mode)
{
    return m_swapChain3->SetFullscreenState(mode, nullptr);
}

} // namespace rhi::d3d12
