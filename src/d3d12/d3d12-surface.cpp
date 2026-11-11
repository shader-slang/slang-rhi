#include "d3d12-surface.h"
#include "d3d12-device.h"
#include "d3d12-texture.h"

namespace rhi::d3d12 {

Result SurfaceImpl::init(DeviceImpl* device, WindowHandle windowHandle)
{
    m_device = device;
    m_queue = m_device->m_queue->m_d3dQueue;
    m_dxgiFactory = device->m_dxgiFactory;
    SLANG_RETURN_ON_FAIL(D3DSurface::init(windowHandle, DXGI_SWAP_EFFECT_FLIP_DISCARD, false));
    SLANG_D3D_RETURN_ON_FAIL_REPORT(
        m_device->m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.writeRef())),
        m_device
    );
    return SLANG_OK;
}

void SurfaceImpl::createSwapchainTextures(uint32_t count)
{
    while (m_frameEvents.size() < count)
    {
        m_frameEvents.push_back(
            CreateEventEx(nullptr, FALSE, CREATE_EVENT_INITIAL_SET | CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS)
        );
    }

    for (uint32_t i = 0; i < count; i++)
    {
        ComPtr<ID3D12Resource> d3dResource;
        m_swapChain->GetBuffer(i, IID_PPV_ARGS(d3dResource.writeRef()));
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
        texture->m_resource.setResource(d3dResource.get());
        texture->m_format = getFormatMapping(textureDesc.format).rtvFormat;
        texture->m_isTypeless = false;
        texture->m_defaultState = D3D12_RESOURCE_STATE_PRESENT;
        texture->m_isSwapchainTexture = true;
        m_textures.push_back(texture);

        SetEvent(m_frameEvents[i]);
    }

    m_swapChain->QueryInterface(m_swapChain3.writeRef());
}

Result SurfaceImpl::configure(const SurfaceConfig& config)
{
    m_swapChain3.setNull();
    for (auto event : m_frameEvents)
        SetEvent(event);
    return D3DSurface::configure(config);
}

Result SurfaceImpl::unconfigure()
{
    // Release the additional interface before the shared implementation destroys the swapchain.
    m_swapChain3.setNull();
    return D3DSurface::unconfigure();
}

Result SurfaceImpl::waitForNextImage()
{
    SLANG_RETURN_ON_FAIL(D3DSurface::waitForNextImage());
    // Readiness alone does not consume the back buffer. Leave its event signaled until acquire
    // so repeated waits and resizing between wait and acquire remain valid.
    uint32_t index = m_swapChain3->GetCurrentBackBufferIndex();
    return WaitForSingleObject(m_frameEvents[index], INFINITE) == WAIT_OBJECT_0 ? SLANG_OK : SLANG_FAIL;
}

Result SurfaceImpl::acquireNextImage(ITexture** outTexture)
{
    *outTexture = nullptr;
    if (!m_configured || m_imageAcquired)
    {
        return SLANG_FAIL;
    }
    auto result = (int)m_swapChain3->GetCurrentBackBufferIndex();
    // Acquire-only callers retain the existing GPU-use wait. After waitForNextImage this event
    // is already signaled, and resetting it marks the start of the newly exposed frame.
    if (WaitForSingleObject(m_frameEvents[result], INFINITE) != WAIT_OBJECT_0)
        return SLANG_FAIL;
    ResetEvent(m_frameEvents[result]);
    m_imageAcquired = true;
    returnComPtrCopy(outTexture, m_textures[result]);
    return SLANG_OK;
}

Result SurfaceImpl::present()
{
    if (!m_configured)
    {
        return SLANG_FAIL;
    }
    // The reuse event must track this frame's signal, not the preceding frame's fence value.
    // Otherwise an early wait could report a back buffer ready while its GPU work still runs.
    uint32_t index = m_swapChain3->GetCurrentBackBufferIndex();
    SLANG_RETURN_ON_FAIL(D3DSurface::present());
    fenceValue++;
    SLANG_D3D_RETURN_ON_FAIL_REPORT(m_queue->Signal(m_fence, fenceValue), m_device);
    SLANG_D3D_RETURN_ON_FAIL_REPORT(m_fence->SetEventOnCompletion(fenceValue, m_frameEvents[index]), m_device);
    return SLANG_OK;
}

} // namespace rhi::d3d12
