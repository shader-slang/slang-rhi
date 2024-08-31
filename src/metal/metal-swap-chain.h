#pragma once

#include "metal-base.h"
#include "metal-command-queue.h"
#include "metal-device.h"
#include "metal-texture.h"

#include "core/short_vector.h"

namespace rhi::metal {

class SwapchainImpl : public ISwapchain, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ISwapchain* getInterface(const Guid& guid);

public:
    RefPtr<DeviceImpl> m_device;
    ISwapchain::Desc m_desc;
    WindowHandle m_windowHandle;
    CA::MetalLayer* m_metalLayer = nullptr;
    short_vector<RefPtr<TextureImpl>> m_images;
    NS::SharedPtr<CA::MetalDrawable> m_currentDrawable;
    Index m_currentImageIndex = -1;
    MTL::PixelFormat m_metalFormat = MTL::PixelFormat::PixelFormatInvalid;

    void getWindowSize(int& widthOut, int& heightOut) const;
    void createImages();

public:
    ~SwapchainImpl();

    Result init(DeviceImpl* device, const ISwapchain::Desc& desc, WindowHandle window);

    virtual SLANG_NO_THROW const Desc& SLANG_MCALL getDesc() override { return m_desc; }
    virtual SLANG_NO_THROW Result SLANG_MCALL getImage(GfxIndex index, ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL resize(GfxCount width, GfxCount height) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL present() override;
    virtual SLANG_NO_THROW int SLANG_MCALL acquireNextImage() override;
    virtual SLANG_NO_THROW bool SLANG_MCALL isOccluded() override { return false; }
    virtual SLANG_NO_THROW Result SLANG_MCALL setFullScreenMode(bool mode) override;
};

} // namespace rhi::metal
