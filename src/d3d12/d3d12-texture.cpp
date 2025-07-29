#include "d3d12-texture.h"
#include "d3d12-device.h"

namespace rhi::d3d12 {


TextureImpl::TextureImpl(Device* device, const TextureDesc& desc)
    : Texture(device, desc)
{
}

TextureImpl::~TextureImpl()
{
    m_defaultView.setNull();
    DeviceImpl* device = getDevice<DeviceImpl>();
    for (auto& srv : m_srvs)
    {
        if (srv.second)
        {
            device->m_cpuCbvSrvUavHeap->free(srv.second);
        }
    }
    for (auto& uav : m_uavs)
    {
        if (uav.second)
        {
            device->m_cpuCbvSrvUavHeap->free(uav.second);
        }
    }
    for (auto& rtv : m_rtvs)
    {
        if (rtv.second)
        {
            device->m_cpuRtvHeap->free(rtv.second);
        }
    }
    for (auto& dsv : m_dsvs)
    {
        if (dsv.second)
        {
            device->m_cpuDsvHeap->free(dsv.second);
        }
    }
    if (m_sharedHandle)
    {
        ::CloseHandle((HANDLE)m_sharedHandle.value);
    }
}

Result TextureImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12Resource;
    outHandle->value = (uint64_t)m_resource.getResource();
    return SLANG_OK;
}

Result TextureImpl::getSharedHandle(NativeHandle* outHandle)
{
#if !SLANG_WINDOWS_FAMILY
    return SLANG_E_NOT_AVAILABLE;
#else
    // Check if a shared handle already exists for this resource.
    if (m_sharedHandle)
    {
        *outHandle = m_sharedHandle;
        return SLANG_OK;
    }

    // If a shared handle doesn't exist, create one and store it.
    ComPtr<ID3D12Device> pDevice;
    auto pResource = m_resource.getResource();
    pResource->GetDevice(IID_PPV_ARGS(pDevice.writeRef()));
    SLANG_RETURN_ON_FAIL(
        pDevice->CreateSharedHandle(pResource, NULL, GENERIC_ALL, nullptr, (HANDLE*)&m_sharedHandle.value)
    );
    m_sharedHandle.type = NativeHandleType::Win32;
    *outHandle = m_sharedHandle;
    return SLANG_OK;
#endif
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

D3D12_CPU_DESCRIPTOR_HANDLE
TextureImpl::getSRV(Format format, TextureType type, TextureAspect aspect, const SubresourceRange& range)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    ViewKey key = {format, type, aspect, range};
    CPUDescriptorAllocation& allocation = m_srvs[key];
    if (allocation)
        return allocation.cpuHandle;

    D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
    viewDesc.Format = m_isTypeless ? getFormatMapping(format).srvFormat : m_format;
    viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    switch (type)
    {
    case TextureType::Texture1D:
        viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
        viewDesc.Texture1D.MostDetailedMip = range.mip;
        viewDesc.Texture1D.MipLevels = range.mipCount;
        break;
    case TextureType::Texture1DArray:
        viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
        viewDesc.Texture1DArray.MostDetailedMip = range.mip;
        viewDesc.Texture1DArray.MipLevels = range.mipCount;
        viewDesc.Texture1DArray.FirstArraySlice = range.layer;
        viewDesc.Texture1DArray.ArraySize = range.layerCount;
        break;
    case TextureType::Texture2D:
        viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        viewDesc.Texture2D.MostDetailedMip = range.mip;
        viewDesc.Texture2D.MipLevels = range.mipCount;
        viewDesc.Texture2D.PlaneSlice = getPlaneSlice(viewDesc.Format, aspect);
        break;
    case TextureType::Texture2DArray:
        viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        viewDesc.Texture2DArray.MostDetailedMip = range.mip;
        viewDesc.Texture2DArray.MipLevels = range.mipCount;
        viewDesc.Texture2DArray.FirstArraySlice = range.layer;
        viewDesc.Texture2DArray.ArraySize = range.layerCount;
        viewDesc.Texture2DArray.PlaneSlice = getPlaneSlice(viewDesc.Format, aspect);
        break;
    case TextureType::Texture2DMS:
        viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
        break;
    case TextureType::Texture2DMSArray:
        viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
        viewDesc.Texture2DMSArray.FirstArraySlice = range.layer;
        viewDesc.Texture2DMSArray.ArraySize = range.layerCount;
        break;
    case TextureType::Texture3D:
        viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        viewDesc.Texture3D.MostDetailedMip = range.mip;
        viewDesc.Texture3D.MipLevels = range.mipCount;
        break;
    case TextureType::TextureCube:
        viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        viewDesc.TextureCube.MostDetailedMip = range.mip;
        viewDesc.TextureCube.MipLevels = range.mipCount;
        break;
    case TextureType::TextureCubeArray:
        viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
        viewDesc.TextureCubeArray.MostDetailedMip = range.mip;
        viewDesc.TextureCubeArray.MipLevels = range.mipCount;
        viewDesc.TextureCubeArray.First2DArrayFace = range.layer;
        viewDesc.TextureCubeArray.NumCubes = range.layerCount / 6;
        break;
    }

    allocation = device->m_cpuCbvSrvUavHeap->allocate();
    device->m_device->CreateShaderResourceView(m_resource.getResource(), &viewDesc, allocation.cpuHandle);

    return allocation.cpuHandle;
}

D3D12_CPU_DESCRIPTOR_HANDLE TextureImpl::getUAV(
    Format format,
    TextureType type,
    TextureAspect aspect,
    const SubresourceRange& range
)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    ViewKey key = {format, type, aspect, range};
    CPUDescriptorAllocation& allocation = m_uavs[key];
    if (allocation)
        return allocation.cpuHandle;

    D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc = {};
    viewDesc.Format = m_isTypeless ? getFormatMapping(format).srvFormat : m_format;
    switch (type)
    {
    case TextureType::Texture1D:
        viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
        viewDesc.Texture1D.MipSlice = range.mip;
        break;
    case TextureType::Texture1DArray:
        viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
        viewDesc.Texture1DArray.MipSlice = range.mip;
        viewDesc.Texture1DArray.ArraySize = range.layerCount;
        viewDesc.Texture1DArray.FirstArraySlice = range.layer;
        break;
    case TextureType::Texture2D:
        viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        viewDesc.Texture2D.MipSlice = range.mip;
        viewDesc.Texture2D.PlaneSlice = getPlaneSlice(viewDesc.Format, aspect);
        break;
    case TextureType::Texture2DArray:
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
        viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        viewDesc.Texture2DArray.MipSlice = range.mip;
        viewDesc.Texture2DArray.ArraySize = range.layerCount;
        viewDesc.Texture2DArray.FirstArraySlice = range.layer;
        viewDesc.Texture2DArray.PlaneSlice = getPlaneSlice(viewDesc.Format, aspect);
        break;
    case TextureType::Texture2DMS:
        viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DMS;
        break;
    case TextureType::Texture2DMSArray:
        viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DMSARRAY;
        viewDesc.Texture2DMSArray.FirstArraySlice = range.layer;
        viewDesc.Texture2DMSArray.ArraySize = range.layerCount;
        break;
    case TextureType::Texture3D:
        viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        viewDesc.Texture3D.MipSlice = range.mip;
        // Set these to 0 and -1 for now to select all depth
        // slices by default, as selecting a subset of depth slices
        // is a concept currently only supported by d3d12.
        viewDesc.Texture3D.FirstWSlice = 0;
        viewDesc.Texture3D.WSize = -1;
        break;
    }

    allocation = device->m_cpuCbvSrvUavHeap->allocate();
    device->m_device->CreateUnorderedAccessView(m_resource.getResource(), nullptr, &viewDesc, allocation.cpuHandle);

    return allocation.cpuHandle;
}

D3D12_CPU_DESCRIPTOR_HANDLE TextureImpl::getRTV(
    Format format,
    TextureType type,
    TextureAspect aspect,
    const SubresourceRange& range
)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    ViewKey key = {format, type, aspect, range};
    CPUDescriptorAllocation& allocation = m_rtvs[key];
    if (allocation)
        return allocation.cpuHandle;

    D3D12_RENDER_TARGET_VIEW_DESC viewDesc = {};
    viewDesc.Format = m_isTypeless ? getFormatMapping(format).rtvFormat : m_format;
    switch (type)
    {
    case TextureType::Texture1D:
        viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
        viewDesc.Texture1D.MipSlice = range.mip;
        break;
    case TextureType::Texture1DArray:
        viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
        viewDesc.Texture1DArray.MipSlice = range.mip;
        viewDesc.Texture1DArray.FirstArraySlice = range.layer;
        viewDesc.Texture1DArray.ArraySize = range.layerCount;
        break;
    case TextureType::Texture2D:
        viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        viewDesc.Texture2D.MipSlice = range.mip;
        viewDesc.Texture2D.PlaneSlice = getPlaneSlice(viewDesc.Format, aspect);
        break;
    case TextureType::Texture2DArray:
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
        viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        viewDesc.Texture2DArray.MipSlice = range.mip;
        viewDesc.Texture2DArray.ArraySize = range.layerCount;
        viewDesc.Texture2DArray.FirstArraySlice = range.layer;
        viewDesc.Texture2DArray.PlaneSlice = getPlaneSlice(viewDesc.Format, aspect);
        break;
    case TextureType::Texture2DMS:
        viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
        break;
    case TextureType::Texture2DMSArray:
        viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
        viewDesc.Texture2DMSArray.ArraySize = range.layerCount;
        viewDesc.Texture2DMSArray.FirstArraySlice = range.layer;
        break;
    case TextureType::Texture3D:
        viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
        viewDesc.Texture3D.MipSlice = range.mip;
        // Set these to 0 and -1 for now to select all depth
        // slices by default, as selecting a subset of depth slices
        // is a concept currently only supported by d3d12.
        viewDesc.Texture3D.FirstWSlice = 0;
        viewDesc.Texture3D.WSize = -1;
        break;
    }

    allocation = device->m_cpuRtvHeap->allocate();
    device->m_device->CreateRenderTargetView(m_resource.getResource(), &viewDesc, allocation.cpuHandle);

    return allocation.cpuHandle;
}

D3D12_CPU_DESCRIPTOR_HANDLE TextureImpl::getDSV(
    Format format,
    TextureType type,
    TextureAspect aspect,
    const SubresourceRange& range
)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    ViewKey key = {format, type, aspect, range};
    CPUDescriptorAllocation& allocation = m_dsvs[key];
    if (allocation)
        return allocation.cpuHandle;

    D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc = {};
    viewDesc.Format = m_isTypeless ? getFormatMapping(format).rtvFormat : m_format;
    switch (type)
    {
    case TextureType::Texture1D:
        viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
        viewDesc.Texture1D.MipSlice = range.mip;
        break;
    case TextureType::Texture1DArray:
        viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
        viewDesc.Texture1DArray.MipSlice = range.mip;
        viewDesc.Texture1DArray.ArraySize = range.layerCount;
        viewDesc.Texture1DArray.FirstArraySlice = range.layer;
        break;
    case TextureType::Texture2D:
        viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        viewDesc.Texture2D.MipSlice = range.mip;
        break;
    case TextureType::Texture2DArray:
        viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        viewDesc.Texture2DArray.MipSlice = range.mip;
        viewDesc.Texture2DArray.ArraySize = range.layerCount;
        viewDesc.Texture2DArray.FirstArraySlice = range.layer;
        break;
    case TextureType::Texture2DMS:
        viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
        break;
    case TextureType::Texture2DMSArray:
        viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
        viewDesc.Texture2DMSArray.ArraySize = range.layerCount;
        viewDesc.Texture2DMSArray.FirstArraySlice = range.layer;
        break;
    case TextureType::Texture3D:
        SLANG_RHI_ASSERT_FAILURE("Not supported");
        break;
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
        viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        viewDesc.Texture2DArray.MipSlice = range.mip;
        viewDesc.Texture2DArray.ArraySize = range.layerCount;
        viewDesc.Texture2DArray.FirstArraySlice = range.layer;
        break;
    }

    allocation = device->m_cpuDsvHeap->allocate();
    device->m_device->CreateDepthStencilView(m_resource.getResource(), &viewDesc, allocation.cpuHandle);

    return allocation.cpuHandle;
}

TextureViewImpl::TextureViewImpl(Device* device, const TextureViewDesc& desc)
    : TextureView(device, desc)
{
}

Result TextureViewImpl::getDescriptorHandle(DescriptorHandleAccess access, DescriptorHandle* outHandle)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    if (!device->m_bindlessDescriptorSet)
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    DescriptorHandle& handle = m_descriptorHandle[access == DescriptorHandleAccess::Read ? 0 : 1];
    if (!handle)
    {
        SLANG_RETURN_ON_FAIL(device->m_bindlessDescriptorSet->allocTextureHandle(this, access, &handle));
    }
    *outHandle = handle;
    return SLANG_OK;
}

Result TextureViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    // TODO return view descriptor
    return m_texture->getNativeHandle(outHandle);
}

D3D12_CPU_DESCRIPTOR_HANDLE TextureViewImpl::getSRV()
{
    if (!m_srv.ptr)
        m_srv = m_texture->getSRV(m_desc.format, m_texture->m_desc.type, m_desc.aspect, m_desc.subresourceRange);
    return m_srv;
}

D3D12_CPU_DESCRIPTOR_HANDLE TextureViewImpl::getUAV()
{
    if (!m_uav.ptr)
        m_uav = m_texture->getUAV(m_desc.format, m_texture->m_desc.type, m_desc.aspect, m_desc.subresourceRange);
    return m_uav;
}

D3D12_CPU_DESCRIPTOR_HANDLE TextureViewImpl::getRTV()
{
    if (!m_rtv.ptr)
        m_rtv = m_texture->getRTV(m_desc.format, m_texture->m_desc.type, m_desc.aspect, m_desc.subresourceRange);
    return m_rtv;
}

D3D12_CPU_DESCRIPTOR_HANDLE TextureViewImpl::getDSV()
{
    if (!m_dsv.ptr)
        m_dsv = m_texture->getDSV(m_desc.format, m_texture->m_desc.type, m_desc.aspect, m_desc.subresourceRange);
    return m_dsv;
}

} // namespace rhi::d3d12
