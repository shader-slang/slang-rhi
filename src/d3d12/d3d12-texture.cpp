#include "d3d12-texture.h"
#include "d3d12-device.h"

namespace rhi::d3d12 {

TextureImpl::TextureImpl(DeviceImpl* device, const TextureDesc& desc)
    : Texture(desc)
    , m_device(device)
    , m_defaultState(D3DUtil::getResourceState(desc.defaultState))
{
}

TextureImpl::~TextureImpl()
{
    for (auto& srv : m_srvs)
    {
        if (srv.second)
        {
            m_device->m_cpuViewHeap->free(srv.second);
        }
    }
    for (auto& uav : m_uavs)
    {
        if (uav.second)
        {
            m_device->m_cpuViewHeap->free(uav.second);
        }
    }
    for (auto& rtv : m_rtvs)
    {
        if (rtv.second)
        {
            m_device->m_rtvAllocator->free(rtv.second);
        }
    }
    for (auto& dsv : m_dsvs)
    {
        if (dsv.second)
        {
            m_device->m_dsvAllocator->free(dsv.second);
        }
    }

    if (m_sharedHandle)
    {
        CloseHandle((HANDLE)m_sharedHandle.value);
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
    if (m_sharedHandle != 0)
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

D3D12Descriptor TextureImpl::getSRV(
    Format format,
    TextureType type,
    TextureAspect aspect,
    const SubresourceRange& range
)
{
    ViewKey key = {format, type, aspect, range};
    D3D12Descriptor& descriptor = m_srvs[key];
    if (descriptor)
        return descriptor;

    bool isArray = m_desc.arrayLength > 1;
    bool isMultiSample = m_desc.sampleCount > 1;
    D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
    viewDesc.Format = D3DUtil::getMapFormat(format);
    viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    switch (type)
    {
    case TextureType::Texture1D:
        if (isArray)
        {
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
            viewDesc.Texture1DArray.MostDetailedMip = range.mipLevel;
            viewDesc.Texture1DArray.MipLevels = range.mipLevelCount;
            viewDesc.Texture1DArray.FirstArraySlice = range.baseArrayLayer;
            viewDesc.Texture1DArray.ArraySize = range.layerCount;
        }
        else
        {
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
            viewDesc.Texture1D.MostDetailedMip = range.mipLevel;
            viewDesc.Texture1D.MipLevels = range.mipLevelCount;
        }
        break;
    case TextureType::Texture2D:
        if (isArray)
        {
            if (isMultiSample)
            {
                viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
                viewDesc.Texture2DMSArray.FirstArraySlice = range.baseArrayLayer;
                viewDesc.Texture2DMSArray.ArraySize = range.layerCount;
            }
            else
            {
                viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                viewDesc.Texture2DArray.MostDetailedMip = range.mipLevel;
                viewDesc.Texture2DArray.MipLevels = range.mipLevelCount;
                viewDesc.Texture2DArray.FirstArraySlice = range.baseArrayLayer;
                viewDesc.Texture2DArray.ArraySize = range.layerCount;
                viewDesc.Texture2DArray.PlaneSlice = D3DUtil::getPlaneSlice(viewDesc.Format, aspect);
            }
        }
        else
        {
            if (isMultiSample)
            {
                viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
            }
            else
            {
                viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                viewDesc.Texture2D.MostDetailedMip = range.mipLevel;
                viewDesc.Texture2D.MipLevels = range.mipLevelCount;
                viewDesc.Texture2D.PlaneSlice = D3DUtil::getPlaneSlice(viewDesc.Format, aspect);
            }
        }
        break;
    case TextureType::Texture3D:
        viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        viewDesc.Texture3D.MostDetailedMip = range.mipLevel;
        viewDesc.Texture3D.MipLevels = range.mipLevelCount;
        break;
    case TextureType::TextureCube:
        if (isArray)
        {
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
            viewDesc.TextureCubeArray.MostDetailedMip = range.mipLevel;
            viewDesc.TextureCubeArray.MipLevels = range.mipLevelCount;
            viewDesc.TextureCubeArray.First2DArrayFace = range.baseArrayLayer;
            viewDesc.TextureCubeArray.NumCubes = range.layerCount / 6;
        }
        else
        {
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            viewDesc.TextureCube.MostDetailedMip = range.mipLevel;
            viewDesc.TextureCube.MipLevels = range.mipLevelCount;
        }
        break;
    }

    m_device->m_cpuViewHeap->allocate(&descriptor);
    m_device->m_device->CreateShaderResourceView(m_resource.getResource(), &viewDesc, descriptor.cpuHandle);

    return descriptor;
}

D3D12Descriptor TextureImpl::getUAV(
    Format format,
    TextureType type,
    TextureAspect aspect,
    const SubresourceRange& range
)
{
    ViewKey key = {format, type, aspect, range};
    D3D12Descriptor& descriptor = m_uavs[key];
    if (descriptor)
        return descriptor;

    bool isArray = m_desc.arrayLength > 1;
    bool isMultiSample = m_desc.sampleCount > 1;
    D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc = {};
    viewDesc.Format =
        getFormatInfo(m_desc.format).isTypeless ? D3DUtil::getMapFormat(m_desc.format) : D3DUtil::getMapFormat(format);
    switch (type)
    {
    case TextureType::Texture1D:
        viewDesc.ViewDimension = isArray ? D3D12_UAV_DIMENSION_TEXTURE1DARRAY : D3D12_UAV_DIMENSION_TEXTURE1D;
        if (isArray)
        {
            viewDesc.Texture1D.MipSlice = range.mipLevel;
        }
        else
        {
            viewDesc.Texture1DArray.MipSlice = range.mipLevel;
            viewDesc.Texture1DArray.ArraySize = range.layerCount;
            viewDesc.Texture1DArray.FirstArraySlice = range.baseArrayLayer;
        }
        break;
    case TextureType::Texture2D:
        viewDesc.ViewDimension = isArray ? D3D12_UAV_DIMENSION_TEXTURE2DARRAY : D3D12_UAV_DIMENSION_TEXTURE2D;
        if (isArray)
        {
            viewDesc.Texture2DArray.MipSlice = range.mipLevel;
            viewDesc.Texture2DArray.ArraySize = range.layerCount;
            viewDesc.Texture2DArray.FirstArraySlice = range.baseArrayLayer;
            viewDesc.Texture2DArray.PlaneSlice = D3DUtil::getPlaneSlice(viewDesc.Format, aspect);
        }
        else
        {
            viewDesc.Texture2D.MipSlice = range.mipLevel;
            viewDesc.Texture2D.PlaneSlice = D3DUtil::getPlaneSlice(viewDesc.Format, aspect);
        }
        break;
    case TextureType::Texture3D:
        viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        viewDesc.Texture3D.MipSlice = range.mipLevel;
        viewDesc.Texture3D.FirstWSlice = range.baseArrayLayer;
        viewDesc.Texture3D.WSize = m_desc.size.depth;
        break;
    case TextureType::TextureCube:
        viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        viewDesc.Texture2DArray.MipSlice = range.mipLevel;
        viewDesc.Texture2DArray.ArraySize = range.layerCount;
        viewDesc.Texture2DArray.FirstArraySlice = range.baseArrayLayer;
        viewDesc.Texture2DArray.PlaneSlice = D3DUtil::getPlaneSlice(viewDesc.Format, aspect);
        break;
    }

    m_device->m_cpuViewHeap->allocate(&descriptor);
    m_device->m_device->CreateUnorderedAccessView(m_resource.getResource(), nullptr, &viewDesc, descriptor.cpuHandle);

    return descriptor;
}

D3D12Descriptor TextureImpl::getRTV(
    Format format,
    TextureType type,
    TextureAspect aspect,
    const SubresourceRange& range
)
{
    ViewKey key = {format, type, aspect, range};
    D3D12Descriptor& descriptor = m_rtvs[key];
    if (descriptor)
        return descriptor;

    bool isArray = m_desc.arrayLength > 1;
    bool isMultiSample = m_desc.sampleCount > 1;
    D3D12_RENDER_TARGET_VIEW_DESC viewDesc = {};
    viewDesc.Format = D3DUtil::getMapFormat(format);
    switch (type)
    {
    case TextureType::Texture1D:
        viewDesc.ViewDimension = isArray ? D3D12_RTV_DIMENSION_TEXTURE1DARRAY : D3D12_RTV_DIMENSION_TEXTURE1D;
        if (isArray)
        {
            viewDesc.Texture1DArray.MipSlice = range.mipLevel;
            viewDesc.Texture1DArray.FirstArraySlice = range.baseArrayLayer;
            viewDesc.Texture1DArray.ArraySize = range.layerCount;
        }
        else
        {
            viewDesc.Texture1D.MipSlice = range.mipLevel;
        }
        break;
    case TextureType::Texture2D:
        if (isMultiSample)
        {
            viewDesc.ViewDimension = isArray ? D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D12_RTV_DIMENSION_TEXTURE2DMS;
            if (isArray)
            {
                viewDesc.Texture2DMSArray.ArraySize = range.layerCount;
                viewDesc.Texture2DMSArray.FirstArraySlice = range.baseArrayLayer;
            }
        }
        else
        {
            viewDesc.ViewDimension = isArray ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY : D3D12_RTV_DIMENSION_TEXTURE2D;
            if (isArray)
            {
                viewDesc.Texture2DArray.MipSlice = range.mipLevel;
                viewDesc.Texture2DArray.ArraySize = range.layerCount;
                viewDesc.Texture2DArray.FirstArraySlice = range.baseArrayLayer;
                viewDesc.Texture2DArray.PlaneSlice = D3DUtil::getPlaneSlice(viewDesc.Format, aspect);
            }
            else
            {
                viewDesc.Texture2D.MipSlice = range.mipLevel;
                viewDesc.Texture2D.PlaneSlice = D3DUtil::getPlaneSlice(viewDesc.Format, aspect);
            }
        }
        break;
    case TextureType::Texture3D:
        viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
        viewDesc.Texture3D.MipSlice = range.mipLevel;
        viewDesc.Texture3D.FirstWSlice = range.baseArrayLayer;
        viewDesc.Texture3D.WSize = m_desc.size.depth;
        break;
    case TextureType::TextureCube:
        viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        viewDesc.Texture2DArray.MipSlice = range.mipLevel;
        viewDesc.Texture2DArray.ArraySize = range.layerCount;
        viewDesc.Texture2DArray.FirstArraySlice = range.baseArrayLayer;
        viewDesc.Texture2DArray.PlaneSlice = D3DUtil::getPlaneSlice(viewDesc.Format, aspect);
        break;
    }

    m_device->m_rtvAllocator->allocate(&descriptor);
    m_device->m_device->CreateRenderTargetView(m_resource.getResource(), &viewDesc, descriptor.cpuHandle);

    return descriptor;
}

D3D12Descriptor TextureImpl::getDSV(
    Format format,
    TextureType type,
    TextureAspect aspect,
    const SubresourceRange& range
)
{
    ViewKey key = {format, type, aspect, range};
    D3D12Descriptor& descriptor = m_dsvs[key];
    if (descriptor)
        return descriptor;

    bool isArray = m_desc.arrayLength > 1;
    bool isMultiSample = m_desc.sampleCount > 1;
    D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc = {};
    viewDesc.Format = D3DUtil::getMapFormat(format);
    switch (type)
    {
    case TextureType::Texture1D:
        viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
        viewDesc.Texture1D.MipSlice = range.mipLevel;
        break;
    case TextureType::Texture2D:
        if (isMultiSample)
        {
            viewDesc.ViewDimension = isArray ? D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D12_DSV_DIMENSION_TEXTURE2DMS;
            if (isArray)
            {
                viewDesc.Texture2DMSArray.ArraySize = range.layerCount;
                viewDesc.Texture2DMSArray.FirstArraySlice = range.baseArrayLayer;
            }
        }
        else
        {
            viewDesc.ViewDimension = isArray ? D3D12_DSV_DIMENSION_TEXTURE2DARRAY : D3D12_DSV_DIMENSION_TEXTURE2D;
            if (isArray)
            {
                viewDesc.Texture2DArray.MipSlice = range.mipLevel;
                viewDesc.Texture2DArray.ArraySize = range.layerCount;
                viewDesc.Texture2DArray.FirstArraySlice = range.baseArrayLayer;
            }
            else
            {
                viewDesc.Texture2D.MipSlice = range.mipLevel;
            }
        }
        break;
    case TextureType::TextureCube:
        viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        viewDesc.Texture2DArray.MipSlice = range.mipLevel;
        viewDesc.Texture2DArray.ArraySize = range.layerCount;
        viewDesc.Texture2DArray.FirstArraySlice = range.baseArrayLayer;
        break;
    }

    m_device->m_dsvAllocator->allocate(&descriptor);
    m_device->m_device->CreateDepthStencilView(m_resource.getResource(), &viewDesc, descriptor.cpuHandle);

    return descriptor;
}


} // namespace rhi::d3d12
