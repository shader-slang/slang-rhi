#pragma once

#include <slang-rhi.h>

#include "../rhi-shared.h"
#include "d3d-util.h"
#include "core/common.h"
#include "core/short_vector.h"

#include <dxgi1_4.h>

namespace rhi {

class D3DSurface : public Surface
{
public:
    Result init(WindowHandle windowHandle, DXGI_SWAP_EFFECT swapEffect, bool allowUnorderedAccess)
    {
        if (windowHandle.type != WindowHandleType::HWND)
        {
            return SLANG_E_INVALID_HANDLE;
        }
        m_windowHandle = (HWND)windowHandle.handleValues[0];
        m_swapEffect = swapEffect;

        m_info.preferredFormat = Format::R8G8B8A8_UNORM;
        m_info.supportedUsage = TextureUsage::RenderTarget | TextureUsage::CopyDestination | TextureUsage::Present;
        if (allowUnorderedAccess)
        {
            m_info.supportedUsage |= TextureUsage::UnorderedAccess;
        }
        static const Format kSupportedFormats[] = {
            Format::R8G8B8A8_UNORM,
            Format::R8G8B8A8_UNORM_SRGB,
            Format::R16G16B16A16_FLOAT,
            Format::R10G10B10A2_UNORM,
        };
        m_info.formats = kSupportedFormats;
        m_info.formatCount = SLANG_COUNT_OF(kSupportedFormats);

        return SLANG_OK;
    }

    Result createSwapchain()
    {
        // Describe the swap chain.
        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        swapChainDesc.BufferCount = m_config.desiredImageCount;
        swapChainDesc.BufferDesc.Width = m_config.width;
        swapChainDesc.BufferDesc.Height = m_config.height;
        swapChainDesc.BufferDesc.Format = D3DUtil::getMapFormat(srgbToLinearFormat(m_config.format));
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        if (is_set(m_info.supportedUsage, TextureUsage::UnorderedAccess))
            swapChainDesc.BufferUsage |= DXGI_USAGE_UNORDERED_ACCESS;
        swapChainDesc.SwapEffect = m_swapEffect;
        swapChainDesc.OutputWindow = m_windowHandle;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.Windowed = TRUE;
        if (!m_config.vsync)
        {
            swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        }

        // Swap chain needs the queue so that it can force a flush on it.
        ComPtr<IDXGIFactory2> dxgiFactory2;
        getDXGIFactory()->QueryInterface(IID_PPV_ARGS(dxgiFactory2.writeRef()));
        if (!dxgiFactory2)
        {
            ComPtr<IDXGISwapChain> swapChain;
            SLANG_RETURN_ON_FAIL(
                getDXGIFactory()->CreateSwapChain(getOwningDevice(), &swapChainDesc, swapChain.writeRef())
            );
            SLANG_RETURN_ON_FAIL(getDXGIFactory()->MakeWindowAssociation(m_windowHandle, DXGI_MWA_NO_ALT_ENTER));
            SLANG_RETURN_ON_FAIL(swapChain->QueryInterface(m_swapChain.writeRef()));
        }
        else
        {
            DXGI_SWAP_CHAIN_DESC1 desc1 = {};
            desc1.BufferCount = swapChainDesc.BufferCount;
            desc1.BufferUsage = swapChainDesc.BufferUsage;
            desc1.Flags = swapChainDesc.Flags;
            desc1.Format = swapChainDesc.BufferDesc.Format;
            desc1.Height = swapChainDesc.BufferDesc.Height;
            desc1.Width = swapChainDesc.BufferDesc.Width;
            desc1.SampleDesc = swapChainDesc.SampleDesc;
            desc1.SwapEffect = swapChainDesc.SwapEffect;
            ComPtr<IDXGISwapChain1> swapChain1;
            SLANG_RETURN_ON_FAIL(dxgiFactory2->CreateSwapChainForHwnd(
                getOwningDevice(),
                m_windowHandle,
                &desc1,
                nullptr,
                nullptr,
                swapChain1.writeRef()
            ));
            SLANG_RETURN_ON_FAIL(swapChain1->QueryInterface(m_swapChain.writeRef()));
        }

        createSwapchainTextures(m_config.desiredImageCount);
        return SLANG_OK;
    }

    void destroySwapchain()
    {
        m_textures.clear();
        m_swapChain.setNull();
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL configure(const SurfaceConfig& config) override
    {
        setConfig(config);
        m_config.format = m_config.format == Format::Unknown ? m_info.preferredFormat : m_config.format;

        m_configured = false;
        destroySwapchain();
        SLANG_RETURN_ON_FAIL(createSwapchain());
        m_configured = true;
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL getCurrentTexture(ITexture** outTexture) override
    {
        if (!m_configured)
        {
            return SLANG_FAIL;
        }

        uint32_t count;
        m_swapChain->GetLastPresentCount(&count);
        uint32_t index = count % m_textures.size();
        returnComPtr(outTexture, m_textures[index]);
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL present() override
    {
        if (!m_configured)
        {
            return SLANG_FAIL;
        }

        const auto res = m_swapChain->Present(m_config.vsync ? 1 : 0, 0);

        // We may want to wait for crash dump completion for some kinds of debugging scenarios
        if (res == DXGI_ERROR_DEVICE_REMOVED || res == DXGI_ERROR_DEVICE_RESET)
        {
            D3DUtil::waitForCrashDumpCompletion(res);
        }

        return SLANG_FAILED(res) ? SLANG_FAIL : SLANG_OK;
    }

public:
    virtual void createSwapchainTextures(uint32_t count) = 0;
    virtual IDXGIFactory* getDXGIFactory() = 0;
    virtual IUnknown* getOwningDevice() = 0;
    HWND m_windowHandle;
    DXGI_SWAP_EFFECT m_swapEffect;
    ComPtr<IDXGISwapChain2> m_swapChain;
    short_vector<RefPtr<Texture>> m_textures;
    bool m_configured = false;
};

} // namespace rhi
