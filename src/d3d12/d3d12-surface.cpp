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
    SLANG_RETURN_ON_FAIL(m_device->m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.writeRef())));
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
    return D3DSurface::unconfigure();
}

Result SurfaceImpl::acquireNextImage(ITexture** outTexture)
{
    if (!m_configured)
    {
        *outTexture = nullptr;
        return SLANG_FAIL;
    }
    auto result = (int)m_swapChain3->GetCurrentBackBufferIndex();
    WaitForSingleObject(m_frameEvents[result], INFINITE);
    ResetEvent(m_frameEvents[result]);
    returnComPtr(outTexture, m_textures[result]);
    return SLANG_OK;
}

Result SurfaceImpl::present()
{
    if (!m_configured)
    {
        return SLANG_FAIL;
    }
    m_fence->SetEventOnCompletion(fenceValue, m_frameEvents[m_swapChain3->GetCurrentBackBufferIndex()]);
    SLANG_RETURN_ON_FAIL(D3DSurface::present());
    fenceValue++;
    m_queue->Signal(m_fence, fenceValue);
    return SLANG_OK;
}

} // namespace rhi::d3d12
