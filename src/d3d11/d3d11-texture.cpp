#include "d3d11-texture.h"
#include "d3d11-device.h"
#include "d3d11-helper-functions.h"

namespace rhi::d3d11 {

ID3D11RenderTargetView* TextureImpl::getRTV(Format format, const SubresourceRange& range_)
{
    SubresourceRange range = resolveSubresourceRange(range_);
    ViewKey key = {format, range};

    std::lock_guard<std::mutex> lock(m_mutex);

    ComPtr<ID3D11RenderTargetView>& rtv = m_rtvs[key];
    if (rtv)
        return rtv;

    SLANG_RETURN_NULL_ON_FAIL(m_device->m_device->CreateRenderTargetView(m_resource, nullptr, rtv.writeRef()));

    return rtv;
}

ID3D11DepthStencilView* TextureImpl::getDSV(Format format, const SubresourceRange& range_)
{
    SubresourceRange range = resolveSubresourceRange(range_);
    ViewKey key = {format, range};

    std::lock_guard<std::mutex> lock(m_mutex);

    ComPtr<ID3D11DepthStencilView>& dsv = m_dsvs[key];
    if (dsv)
        return dsv;

    SLANG_RETURN_NULL_ON_FAIL(m_device->m_device->CreateDepthStencilView(m_resource, nullptr, dsv.writeRef()));

    return dsv;
}

ID3D11ShaderResourceView* TextureImpl::getSRV(Format format, const SubresourceRange& range_)
{
    SubresourceRange range = resolveSubresourceRange(range_);
    ViewKey key = {format, range};

    std::lock_guard<std::mutex> lock(m_mutex);

    ComPtr<ID3D11ShaderResourceView>& srv = m_srvs[key];
    if (srv)
        return srv;


    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    initSrvDesc(m_desc, D3DUtil::getMapFormat(format), srvDesc);

    SLANG_RETURN_NULL_ON_FAIL(m_device->m_device->CreateShaderResourceView(m_resource, &srvDesc, srv.writeRef()));

    return srv;
}

ID3D11UnorderedAccessView* TextureImpl::getUAV(Format format, const SubresourceRange& range_)
{
    SubresourceRange range = resolveSubresourceRange(range_);
    ViewKey key = {format, range};

    std::lock_guard<std::mutex> lock(m_mutex);

    ComPtr<ID3D11UnorderedAccessView>& uav = m_uavs[key];
    if (uav)
        return uav;

    SLANG_RETURN_NULL_ON_FAIL(m_device->m_device->CreateUnorderedAccessView(m_resource, nullptr, uav.writeRef()));

    return uav;
}

Result DeviceImpl::createTexture(const TextureDesc& descIn, const SubresourceData* initData, ITexture** outTexture)
{
    TextureDesc srcDesc = fixupTextureDesc(descIn);

    const DXGI_FORMAT format = D3DUtil::getMapFormat(srcDesc.format);
    if (format == DXGI_FORMAT_UNKNOWN)
    {
        return SLANG_FAIL;
    }

    UINT bindFlags = _calcResourceBindFlags(srcDesc.usage);

    // Set up the initialize data
    std::vector<D3D11_SUBRESOURCE_DATA> subRes;
    D3D11_SUBRESOURCE_DATA* subresourcesPtr = nullptr;
    if (initData)
    {
        uint32_t arrayLayerCount = srcDesc.arrayLength * (srcDesc.type == TextureType::TextureCube ? 6 : 1);
        subRes.resize(srcDesc.mipLevelCount * arrayLayerCount);
        {
            uint32_t subresourceIndex = 0;
            for (uint32_t i = 0; i < arrayLayerCount; i++)
            {
                for (uint32_t j = 0; j < srcDesc.mipLevelCount; j++)
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

    UINT accessFlags = _calcResourceAccessFlags(srcDesc.memoryType);

    RefPtr<TextureImpl> texture(new TextureImpl(this, srcDesc));

    switch (srcDesc.type)
    {
    case TextureType::Texture1D:
    {
        D3D11_TEXTURE1D_DESC desc = {0};
        desc.BindFlags = bindFlags;
        desc.CPUAccessFlags = accessFlags;
        desc.Format = format;
        desc.MiscFlags = 0;
        desc.MipLevels = srcDesc.mipLevelCount;
        desc.ArraySize = srcDesc.arrayLength;
        desc.Width = srcDesc.size.width;
        desc.Usage = D3D11_USAGE_DEFAULT;

        ComPtr<ID3D11Texture1D> texture1D;
        SLANG_RETURN_ON_FAIL(m_device->CreateTexture1D(&desc, subresourcesPtr, texture1D.writeRef()));

        texture->m_resource = texture1D;
        break;
    }
    case TextureType::TextureCube:
    case TextureType::Texture2D:
    {
        D3D11_TEXTURE2D_DESC desc = {0};
        desc.BindFlags = bindFlags;
        desc.CPUAccessFlags = accessFlags;
        desc.Format = format;
        desc.MiscFlags = 0;
        desc.MipLevels = srcDesc.mipLevelCount;
        desc.ArraySize = srcDesc.arrayLength * (srcDesc.type == TextureType::TextureCube ? 6 : 1);

        desc.Width = srcDesc.size.width;
        desc.Height = srcDesc.size.height;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.SampleDesc.Count = srcDesc.sampleCount;
        desc.SampleDesc.Quality = srcDesc.sampleQuality;

        if (srcDesc.type == TextureType::TextureCube)
        {
            desc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
        }

        ComPtr<ID3D11Texture2D> texture2D;
        SLANG_RETURN_ON_FAIL(m_device->CreateTexture2D(&desc, subresourcesPtr, texture2D.writeRef()));

        texture->m_resource = texture2D;
        break;
    }
    case TextureType::Texture3D:
    {
        D3D11_TEXTURE3D_DESC desc = {0};
        desc.BindFlags = bindFlags;
        desc.CPUAccessFlags = accessFlags;
        desc.Format = format;
        desc.MiscFlags = 0;
        desc.MipLevels = srcDesc.mipLevelCount;
        desc.Width = srcDesc.size.width;
        desc.Height = srcDesc.size.height;
        desc.Depth = srcDesc.size.depth;
        desc.Usage = D3D11_USAGE_DEFAULT;

        ComPtr<ID3D11Texture3D> texture3D;
        SLANG_RETURN_ON_FAIL(m_device->CreateTexture3D(&desc, subresourcesPtr, texture3D.writeRef()));

        texture->m_resource = texture3D;
        break;
    }
    default:
        return SLANG_FAIL;
    }

    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

Result DeviceImpl::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    RefPtr<TextureViewImpl> view = new TextureViewImpl(desc);
    view->m_texture = checked_cast<TextureImpl*>(texture);
    if (view->m_desc.format == Format::Unknown)
        view->m_desc.format = view->m_texture->m_desc.format;
    view->m_desc.subresourceRange = view->m_texture->resolveSubresourceRange(desc.subresourceRange);
    returnComPtr(outView, view);
    return SLANG_OK;
}

} // namespace rhi::d3d11
