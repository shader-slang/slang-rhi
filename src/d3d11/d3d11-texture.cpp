#include "d3d11-texture.h"
#include "d3d11-device.h"
#include "d3d11-helper-functions.h"

namespace rhi::d3d11 {

ID3D11RenderTargetView* TextureImpl::getRTV(Format format, const SubresourceRange& range_)
{
    SubresourceRange range = resolveSubresourceRange(range_);
    ViewKey key = {format, range};

    ComPtr<ID3D11RenderTargetView>& rtv = m_rtvs[key];
    if (rtv)
        return rtv;

    DeviceImpl* device = static_cast<DeviceImpl*>(m_device.get());
    SLANG_RETURN_NULL_ON_FAIL(device->m_device->CreateRenderTargetView(m_resource, nullptr, rtv.writeRef()));

    return rtv;
}

ID3D11DepthStencilView* TextureImpl::getDSV(Format format, const SubresourceRange& range_)
{
    SubresourceRange range = resolveSubresourceRange(range_);
    ViewKey key = {format, range};

    ComPtr<ID3D11DepthStencilView>& dsv = m_dsvs[key];
    if (dsv)
        return dsv;

    DeviceImpl* device = static_cast<DeviceImpl*>(m_device.get());
    SLANG_RETURN_NULL_ON_FAIL(device->m_device->CreateDepthStencilView(m_resource, nullptr, dsv.writeRef()));

    return dsv;
}

ID3D11ShaderResourceView* TextureImpl::getSRV(Format format, const SubresourceRange& range_)
{
    SubresourceRange range = resolveSubresourceRange(range_);
    ViewKey key = {format, range};

    ComPtr<ID3D11ShaderResourceView>& srv = m_srvs[key];
    if (srv)
        return srv;


    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    initSrvDesc(m_desc, D3DUtil::getMapFormat(format), srvDesc);

    DeviceImpl* device = static_cast<DeviceImpl*>(m_device.get());
    SLANG_RETURN_NULL_ON_FAIL(device->m_device->CreateShaderResourceView(m_resource, &srvDesc, srv.writeRef()));

    return srv;
}

ID3D11UnorderedAccessView* TextureImpl::getUAV(Format format, const SubresourceRange& range_)
{
    SubresourceRange range = resolveSubresourceRange(range_);
    ViewKey key = {format, range};

    ComPtr<ID3D11UnorderedAccessView>& uav = m_uavs[key];
    if (uav)
        return uav;

    DeviceImpl* device = static_cast<DeviceImpl*>(m_device.get());
    SLANG_RETURN_NULL_ON_FAIL(device->m_device->CreateUnorderedAccessView(m_resource, nullptr, uav.writeRef()));

    return uav;
}


} // namespace rhi::d3d11
