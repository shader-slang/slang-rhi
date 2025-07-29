#include "d3d11-texture.h"
#include "d3d11-device.h"
#include "d3d11-utils.h"

namespace rhi::d3d11 {

TextureImpl::TextureImpl(Device* device, const TextureDesc& desc)
    : Texture(device, desc)
{
}

TextureImpl::~TextureImpl()
{
    m_defaultView.setNull();
}

Result TextureImpl::getDefaultView(ITextureView** outTextureView)
{
    if (!m_defaultView)
    {
        SLANG_RETURN_ON_FAIL(m_device->createTextureView(this, {}, (ITextureView**)m_defaultView.writeRef()));
        m_defaultView->setInternalReferenceCount(1);
    }
    returnComPtr(outTextureView, m_defaultView);
    return SLANG_OK;
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

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = getFormatMapping(m_desc.format).rtvFormat;
    switch (m_desc.type)
    {
    case TextureType::Texture1D:
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1D;
        rtvDesc.Texture1D.MipSlice = range.mip;
        break;
    case TextureType::Texture1DArray:
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1DARRAY;
        rtvDesc.Texture1DArray.MipSlice = range.mip;
        rtvDesc.Texture1DArray.FirstArraySlice = range.layer;
        rtvDesc.Texture1DArray.ArraySize = range.layerCount;
        break;
    case TextureType::Texture2D:
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = range.mip;
        break;
    case TextureType::Texture2DArray:
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        rtvDesc.Texture2DArray.MipSlice = range.mip;
        rtvDesc.Texture2DArray.FirstArraySlice = range.layer;
        rtvDesc.Texture2DArray.ArraySize = range.layerCount;
        break;
    case TextureType::Texture2DMS:
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
        break;
    case TextureType::Texture2DMSArray:
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
        rtvDesc.Texture2DMSArray.FirstArraySlice = range.layer;
        rtvDesc.Texture2DMSArray.ArraySize = range.layerCount;
        break;
    case TextureType::Texture3D:
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
        rtvDesc.Texture3D.MipSlice = range.mip;
        rtvDesc.Texture3D.FirstWSlice = 0;
        rtvDesc.Texture3D.WSize = -1;
        break;
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
        break;
    }

    SLANG_RETURN_NULL_ON_FAIL(device->m_device->CreateRenderTargetView(m_resource, &rtvDesc, rtv.writeRef()));

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

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = getFormatMapping(m_desc.format).rtvFormat;
    switch (m_desc.type)
    {
    case TextureType::Texture1D:
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE1D;
        dsvDesc.Texture1D.MipSlice = range.mip;
        break;
    case TextureType::Texture1DArray:
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE1DARRAY;
        dsvDesc.Texture1DArray.MipSlice = range.mip;
        dsvDesc.Texture1DArray.FirstArraySlice = range.layer;
        dsvDesc.Texture1DArray.ArraySize = range.layerCount;
        break;
    case TextureType::Texture2D:
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = range.mip;
        break;
    case TextureType::Texture2DArray:
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.MipSlice = range.mip;
        dsvDesc.Texture2DArray.FirstArraySlice = range.layer;
        dsvDesc.Texture2DArray.ArraySize = range.layerCount;
        break;
    case TextureType::Texture2DMS:
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
        break;
    case TextureType::Texture2DMSArray:
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY;
        dsvDesc.Texture2DMSArray.FirstArraySlice = range.layer;
        dsvDesc.Texture2DMSArray.ArraySize = range.layerCount;
        break;
    case TextureType::Texture3D:
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
        break;
    }

    SLANG_RETURN_NULL_ON_FAIL(device->m_device->CreateDepthStencilView(m_resource, &dsvDesc, dsv.writeRef()));

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

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = m_isTypeless ? getFormatMapping(m_desc.format).srvFormat : m_format;
    switch (m_desc.type)
    {
    case TextureType::Texture1D:
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
        srvDesc.Texture1D.MostDetailedMip = range.mip;
        srvDesc.Texture1D.MipLevels = range.mipCount;
        break;
    case TextureType::Texture1DArray:
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
        srvDesc.Texture1DArray.MostDetailedMip = range.mip;
        srvDesc.Texture1DArray.MipLevels = range.mipCount;
        srvDesc.Texture1DArray.FirstArraySlice = range.layer;
        srvDesc.Texture1DArray.ArraySize = range.layerCount;
        break;
    case TextureType::Texture2D:
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = range.mip;
        srvDesc.Texture2D.MipLevels = range.mipCount;
        break;
    case TextureType::Texture2DArray:
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Texture2DArray.MostDetailedMip = range.mip;
        srvDesc.Texture2DArray.MipLevels = range.mipCount;
        srvDesc.Texture2DArray.FirstArraySlice = range.layer;
        srvDesc.Texture2DArray.ArraySize = range.layerCount;
        break;
    case TextureType::Texture2DMS:
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
        break;
    case TextureType::Texture2DMSArray:
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
        srvDesc.Texture2DMSArray.FirstArraySlice = range.layer;
        srvDesc.Texture2DMSArray.ArraySize = range.layerCount;
        break;
    case TextureType::Texture3D:
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MostDetailedMip = range.mip;
        srvDesc.Texture3D.MipLevels = range.mipCount;
        break;
    case TextureType::TextureCube:
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = range.mip;
        srvDesc.TextureCube.MipLevels = range.mipCount;
        break;
    case TextureType::TextureCubeArray:
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
        srvDesc.TextureCubeArray.MostDetailedMip = range.mip;
        srvDesc.TextureCubeArray.MipLevels = range.mipCount;
        srvDesc.TextureCubeArray.First2DArrayFace = range.layer;
        srvDesc.TextureCubeArray.NumCubes = range.layerCount / 6;
        break;
    }

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

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = m_isTypeless ? getFormatMapping(m_desc.format).srvFormat : m_format;
    switch (m_desc.type)
    {
    case TextureType::Texture1D:
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1D;
        uavDesc.Texture1D.MipSlice = range.mip;
        break;
    case TextureType::Texture1DArray:
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1DARRAY;
        uavDesc.Texture1DArray.MipSlice = range.mip;
        uavDesc.Texture1DArray.FirstArraySlice = range.layer;
        uavDesc.Texture1DArray.ArraySize = range.layerCount;
        break;
    case TextureType::Texture2D:
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = range.mip;
        break;
    case TextureType::Texture2DArray:
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Texture2DArray.MipSlice = range.mip;
        uavDesc.Texture2DArray.FirstArraySlice = range.layer;
        uavDesc.Texture2DArray.ArraySize = range.layerCount;
        break;
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
        break;
    case TextureType::Texture3D:
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.MipSlice = range.mip;
        uavDesc.Texture3D.FirstWSlice = 0;
        uavDesc.Texture3D.WSize = -1;
        break;
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
        break;
    }

    SLANG_RETURN_NULL_ON_FAIL(device->m_device->CreateUnorderedAccessView(m_resource, &uavDesc, uav.writeRef()));

    return uav;
}

Result DeviceImpl::createTexture(const TextureDesc& desc_, const SubresourceData* initData, ITexture** outTexture)
{
    TextureDesc desc = fixupTextureDesc(desc_);

    RefPtr<TextureImpl> texture(new TextureImpl(this, desc));

    uint32_t mipCount = desc.mipCount;
    uint32_t layerCount = desc.getLayerCount();

    bool isTypeless = is_set(desc.usage, TextureUsage::Typeless);
    if (isDepthFormat(desc.format) &&
        (is_set(desc.usage, TextureUsage::ShaderResource) || is_set(desc.usage, TextureUsage::UnorderedAccess)))
    {
        isTypeless = true;
    }
    const DXGI_FORMAT format =
        isTypeless ? getFormatMapping(desc.format).typelessFormat : getFormatMapping(desc.format).rtvFormat;
    if (format == DXGI_FORMAT_UNKNOWN)
    {
        return SLANG_FAIL;
    }

    texture->m_format = format;
    texture->m_isTypeless = isTypeless;

    UINT bindFlags = _calcResourceBindFlags(desc.usage);

    // Set up the initialize data
    std::vector<D3D11_SUBRESOURCE_DATA> subRes;
    D3D11_SUBRESOURCE_DATA* subresourcesPtr = nullptr;
    if (initData)
    {
        subRes.resize(mipCount * layerCount);
        {
            uint32_t subresourceIndex = 0;
            for (uint32_t i = 0; i < layerCount; i++)
            {
                for (uint32_t j = 0; j < mipCount; j++)
                {
                    D3D11_SUBRESOURCE_DATA& data = subRes[subresourceIndex];
                    auto& srcData = initData[subresourceIndex];

                    data.pSysMem = srcData.data;
                    data.SysMemPitch = UINT(srcData.rowPitch);
                    data.SysMemSlicePitch = UINT(srcData.slicePitch);

                    subresourceIndex++;
                }
            }
        }
        subresourcesPtr = subRes.data();
    }

    UINT accessFlags = _calcResourceAccessFlags(desc.memoryType);
    D3D11_USAGE d3dUsage = D3D11_USAGE_DEFAULT;

    // If texture will be used for upload, then:
    //  - if pure copying, create as a staging texture (D3D11_USAGE_STAGING)
    //  - if not, create as a dynamic texture (D3D11_USAGE_DYNAMIC) unless unordered access is specified
    if (desc.memoryType == MemoryType::Upload)
    {
        accessFlags |= D3D11_CPU_ACCESS_WRITE;
        if ((desc.usage & (TextureUsage::CopySource | TextureUsage::CopyDestination)) == desc.usage)
        {
            d3dUsage = D3D11_USAGE_STAGING;
            accessFlags |= D3D11_CPU_ACCESS_READ; // Support read, so can be mapped as read/write
        }
        else if (!is_set(desc.usage, TextureUsage::UnorderedAccess))
        {
            d3dUsage = D3D11_USAGE_DYNAMIC;
        }
    }

    // If texture will be used for read-back, then it must be staging.
    if (desc.memoryType == MemoryType::ReadBack)
    {
        accessFlags |= D3D11_CPU_ACCESS_READ;
        d3dUsage = D3D11_USAGE_STAGING;
    }

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
        d3dDesc.MipLevels = mipCount;
        d3dDesc.ArraySize = layerCount;
        d3dDesc.Width = desc.size.width;
        d3dDesc.Usage = d3dUsage;

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
        d3dDesc.MipLevels = mipCount;
        d3dDesc.ArraySize = layerCount;

        d3dDesc.Width = desc.size.width;
        d3dDesc.Height = desc.size.height;
        d3dDesc.Usage = d3dUsage;
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
        d3dDesc.MipLevels = mipCount;
        d3dDesc.Width = desc.size.width;
        d3dDesc.Height = desc.size.height;
        d3dDesc.Depth = desc.size.depth;
        d3dDesc.Usage = d3dUsage;

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
