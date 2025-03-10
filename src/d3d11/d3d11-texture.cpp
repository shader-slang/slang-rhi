#include "d3d11-texture.h"
#include "d3d11-device.h"
#include "d3d11-helper-functions.h"

namespace rhi::d3d11 {

TextureImpl::TextureImpl(Device* device, const TextureDesc& desc)
    : Texture(device, desc)
{
}

ID3D11RenderTargetView* TextureImpl::getRTV(Format format, const SubresourceRange& range_)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    SubresourceRange range = resolveSubresourceRange(range_);
    ViewKey key = {format, range};

    std::lock_guard<std::mutex> lock(m_mutex);

    ComPtr<ID3D11RenderTargetView>& rtv = m_rtvs[key];
    if (rtv)
        return rtv;

    SLANG_RETURN_NULL_ON_FAIL(device->m_device->CreateRenderTargetView(m_resource, nullptr, rtv.writeRef()));

    return rtv;
}

ID3D11DepthStencilView* TextureImpl::getDSV(Format format, const SubresourceRange& range_)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    SubresourceRange range = resolveSubresourceRange(range_);
    ViewKey key = {format, range};

    std::lock_guard<std::mutex> lock(m_mutex);

    ComPtr<ID3D11DepthStencilView>& dsv = m_dsvs[key];
    if (dsv)
        return dsv;

    SLANG_RETURN_NULL_ON_FAIL(device->m_device->CreateDepthStencilView(m_resource, nullptr, dsv.writeRef()));

    return dsv;
}

ID3D11ShaderResourceView* TextureImpl::getSRV(Format format, const SubresourceRange& range_)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    SubresourceRange range = resolveSubresourceRange(range_);
    ViewKey key = {format, range};

    std::lock_guard<std::mutex> lock(m_mutex);

    ComPtr<ID3D11ShaderResourceView>& srv = m_srvs[key];
    if (srv)
        return srv;


    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    initSrvDesc(m_desc, D3DUtil::getMapFormat(format), srvDesc);

    SLANG_RETURN_NULL_ON_FAIL(device->m_device->CreateShaderResourceView(m_resource, &srvDesc, srv.writeRef()));

    return srv;
}

ID3D11UnorderedAccessView* TextureImpl::getUAV(Format format, const SubresourceRange& range_)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    SubresourceRange range = resolveSubresourceRange(range_);
    ViewKey key = {format, range};

    std::lock_guard<std::mutex> lock(m_mutex);

    ComPtr<ID3D11UnorderedAccessView>& uav = m_uavs[key];
    if (uav)
        return uav;

    SLANG_RETURN_NULL_ON_FAIL(device->m_device->CreateUnorderedAccessView(m_resource, nullptr, uav.writeRef()));

    return uav;
}

Result DeviceImpl::createTexture(const TextureDesc& desc_, const SubresourceData* initData, ITexture** outTexture)
{
    TextureDesc desc = fixupTextureDesc(desc_);

    RefPtr<TextureImpl> texture(new TextureImpl(this, desc));

    uint32_t mipLevelCount = desc.mipLevelCount;
    uint32_t layerCount = desc.getLayerCount();

    const DXGI_FORMAT format = D3DUtil::getMapFormat(desc.format);
    if (format == DXGI_FORMAT_UNKNOWN)
    {
        return SLANG_FAIL;
    }

    UINT bindFlags = _calcResourceBindFlags(desc.usage);

    // Set up the initialize data
    std::vector<D3D11_SUBRESOURCE_DATA> subRes;
    D3D11_SUBRESOURCE_DATA* subresourcesPtr = nullptr;
    if (initData)
    {
        subRes.resize(mipLevelCount * layerCount);
        {
            uint32_t subresourceIndex = 0;
            for (uint32_t i = 0; i < layerCount; i++)
            {
                for (uint32_t j = 0; j < mipLevelCount; j++)
                {
                    D3D11_SUBRESOURCE_DATA& data = subRes[subresourceIndex];
                    auto& srcData = initData[subresourceIndex];

                    data.pSysMem = srcData.data;
                    data.SysMemPitch = UINT(srcData.strideY);
                    data.SysMemSlicePitch = UINT(srcData.strideZ);

                    subresourceIndex++;
                }
            }
        }
        subresourcesPtr = subRes.data();
    }

    UINT accessFlags = _calcResourceAccessFlags(desc.memoryType);

    switch (desc.type)
    {
    case TextureType::Texture1D:
    case TextureType::Texture1DArray:
    {
        D3D11_TEXTURE1D_DESC d3dDesc = {0};
        d3dDesc.BindFlags = bindFlags;
        d3dDesc.CPUAccessFlags = accessFlags;
        d3dDesc.Format = format;
        d3dDesc.MiscFlags = 0;
        d3dDesc.MipLevels = mipLevelCount;
        d3dDesc.ArraySize = layerCount;
        d3dDesc.Width = desc.size.width;
        d3dDesc.Usage = D3D11_USAGE_DEFAULT;

        ComPtr<ID3D11Texture1D> texture1D;
        SLANG_RETURN_ON_FAIL(m_device->CreateTexture1D(&d3dDesc, subresourcesPtr, texture1D.writeRef()));

        texture->m_resource = texture1D;
        break;
    }
    case TextureType::Texture2D:
    case TextureType::Texture2DArray:
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
    {
        D3D11_TEXTURE2D_DESC d3dDesc = {0};
        d3dDesc.BindFlags = bindFlags;
        d3dDesc.CPUAccessFlags = accessFlags;
        d3dDesc.Format = format;
        d3dDesc.MiscFlags = 0;
        d3dDesc.MipLevels = mipLevelCount;
        d3dDesc.ArraySize = layerCount;

        d3dDesc.Width = desc.size.width;
        d3dDesc.Height = desc.size.height;
        d3dDesc.Usage = D3D11_USAGE_DEFAULT;
        d3dDesc.SampleDesc.Count = desc.sampleCount;
        d3dDesc.SampleDesc.Quality = desc.sampleQuality;
        if (desc.type == TextureType::TextureCube || desc.type == TextureType::TextureCubeArray)
        {
            d3dDesc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
        }

        ComPtr<ID3D11Texture2D> texture2D;
        SLANG_RETURN_ON_FAIL(m_device->CreateTexture2D(&d3dDesc, subresourcesPtr, texture2D.writeRef()));

        texture->m_resource = texture2D;
        break;
    }
    case TextureType::Texture3D:
    {
        D3D11_TEXTURE3D_DESC d3dDesc = {0};
        d3dDesc.BindFlags = bindFlags;
        d3dDesc.CPUAccessFlags = accessFlags;
        d3dDesc.Format = format;
        d3dDesc.MiscFlags = 0;
        d3dDesc.MipLevels = mipLevelCount;
        d3dDesc.Width = desc.size.width;
        d3dDesc.Height = desc.size.height;
        d3dDesc.Depth = desc.size.depth;
        d3dDesc.Usage = D3D11_USAGE_DEFAULT;

        ComPtr<ID3D11Texture3D> texture3D;
        SLANG_RETURN_ON_FAIL(m_device->CreateTexture3D(&d3dDesc, subresourcesPtr, texture3D.writeRef()));

        texture->m_resource = texture3D;
        break;
    }
    }

    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

TextureViewImpl::TextureViewImpl(Device* device, const TextureViewDesc& desc)
    : TextureView(device, desc)
{
}

Result DeviceImpl::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    RefPtr<TextureViewImpl> view = new TextureViewImpl(this, desc);
    view->m_texture = checked_cast<TextureImpl*>(texture);
    if (view->m_desc.format == Format::Undefined)
        view->m_desc.format = view->m_texture->m_desc.format;
    view->m_desc.subresourceRange = view->m_texture->resolveSubresourceRange(desc.subresourceRange);
    returnComPtr(outView, view);
    return SLANG_OK;
}

} // namespace rhi::d3d11
