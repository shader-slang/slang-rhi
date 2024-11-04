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

Result TextureViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    // TODO return view descriptor
    return m_texture->getNativeHandle(outHandle);
}

D3D12Descriptor TextureViewImpl::getSRV()
{
    if (!m_srv)
        m_srv = m_texture->getSRV(m_desc.format, m_texture->m_desc.type, m_desc.aspect, m_desc.subresourceRange);
    return m_srv;
}

D3D12Descriptor TextureViewImpl::getUAV()
{
    if (!m_uav)
        m_uav = m_texture->getUAV(m_desc.format, m_texture->m_desc.type, m_desc.aspect, m_desc.subresourceRange);
    return m_uav;
}

D3D12Descriptor TextureViewImpl::getRTV()
{
    if (!m_rtv)
        m_rtv = m_texture->getRTV(m_desc.format, m_texture->m_desc.type, m_desc.aspect, m_desc.subresourceRange);
    return m_rtv;
}

D3D12Descriptor TextureViewImpl::getDSV()
{
    if (!m_dsv)
        m_dsv = m_texture->getDSV(m_desc.format, m_texture->m_desc.type, m_desc.aspect, m_desc.subresourceRange);
    return m_dsv;
}


#if 0
ResourceViewInternalImpl::~ResourceViewInternalImpl()
{
    if (m_descriptor.cpuHandle.ptr)
        m_allocator->free(m_descriptor);
    for (auto desc : m_mapBufferStrideToDescriptor)
    {
        m_allocator->free(desc.second);
    }
}

Result createD3D12BufferDescriptor(
    BufferImpl* buffer,
    BufferImpl* counterBuffer,
    IResourceView::Desc const& desc,
    uint32_t bufferStride,
    DeviceImpl* device,
    D3D12GeneralExpandingDescriptorHeap* descriptorHeap,
    D3D12Descriptor* outDescriptor
)
{

    auto resourceImpl = (BufferImpl*)buffer;
    auto resourceDesc = *resourceImpl->getDesc();
    const auto counterResourceImpl = checked_cast<BufferImpl*>(counterBuffer);

    uint64_t offset = desc.bufferRange.offset;
    uint64_t size = desc.bufferRange.size == 0 ? buffer->getDesc()->size - offset : desc.bufferRange.size;

    switch (desc.type)
    {
    default:
        return SLANG_FAIL;

    case IResourceView::Type::UnorderedAccess:
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Format = D3DUtil::getMapFormat(desc.format);
        if (bufferStride)
        {
            uavDesc.Buffer.FirstElement = offset / bufferStride;
            uavDesc.Buffer.NumElements = UINT(size / bufferStride);
            uavDesc.Buffer.StructureByteStride = bufferStride;
        }
        else if (desc.format == Format::Unknown)
        {
            uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            uavDesc.Buffer.FirstElement = offset / 4;
            uavDesc.Buffer.NumElements = UINT(size / 4);
            uavDesc.Buffer.Flags |= D3D12_BUFFER_UAV_FLAG_RAW;
        }
        else
        {
            const FormatInfo& formatInfo = getFormatInfo(desc.format);
            SLANG_RHI_ASSERT(formatInfo.pixelsPerBlock == 1);
            uavDesc.Buffer.FirstElement = offset / formatInfo.blockSizeInBytes;
            uavDesc.Buffer.NumElements = UINT(size / formatInfo.blockSizeInBytes);
        }

        if (size >= (1ull << 32) - 8)
        {
            // D3D12 does not support view descriptors that has size near 4GB.
            // We will not create actual SRV/UAVs for such large buffers.
            // However, a buffer this large can still be bound as root parameter.
            // So instead of failing, we quietly ignore descriptor creation.
            outDescriptor->cpuHandle.ptr = 0;
        }
        else
        {
            SLANG_RETURN_ON_FAIL(descriptorHeap->allocate(outDescriptor));
            device->m_device->CreateUnorderedAccessView(
                resourceImpl->m_resource,
                counterResourceImpl ? counterResourceImpl->m_resource.getResource() : nullptr,
                &uavDesc,
                outDescriptor->cpuHandle
            );
        }
    }
    break;

    case IResourceView::Type::ShaderResource:
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Format = D3DUtil::getMapFormat(desc.format);
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (bufferStride)
        {
            srvDesc.Buffer.FirstElement = offset / bufferStride;
            srvDesc.Buffer.NumElements = UINT(size / bufferStride);
            srvDesc.Buffer.StructureByteStride = bufferStride;
        }
        else if (desc.format == Format::Unknown)
        {
            srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            srvDesc.Buffer.FirstElement = offset / 4;
            srvDesc.Buffer.NumElements = UINT(size / 4);
            srvDesc.Buffer.Flags |= D3D12_BUFFER_SRV_FLAG_RAW;
        }
        else
        {
            const FormatInfo& formatInfo = getFormatInfo(desc.format);
            SLANG_RHI_ASSERT(formatInfo.pixelsPerBlock == 1);
            srvDesc.Buffer.FirstElement = offset / formatInfo.blockSizeInBytes;
            srvDesc.Buffer.NumElements = UINT(size / formatInfo.blockSizeInBytes);
        }

        if (size >= (1ull << 32) - 8)
        {
            // D3D12 does not support view descriptors that has size near 4GB.
            // We will not create actual SRV/UAVs for such large buffers.
            // However, a buffer this large can still be bound as root parameter.
            // So instead of failing, we quietly ignore descriptor creation.
            outDescriptor->cpuHandle.ptr = 0;
        }
        else
        {
            SLANG_RETURN_ON_FAIL(descriptorHeap->allocate(outDescriptor));
            device->m_device->CreateShaderResourceView(resourceImpl->m_resource, &srvDesc, outDescriptor->cpuHandle);
        }
    }
    break;
    }
    return SLANG_OK;
}

Result ResourceViewInternalImpl::getBufferDescriptorForBinding(
    DeviceImpl* device,
    ResourceViewImpl* view,
    uint32_t bufferStride,
    D3D12Descriptor& outDescriptor
)
{
    // Look for an existing descriptor from the cache if it exists.
    auto it = m_mapBufferStrideToDescriptor.find(bufferStride);
    if (it != m_mapBufferStrideToDescriptor.end())
    {
        outDescriptor = it->second;
        return SLANG_OK;
    }

    // We need to create and cache a d3d12 descriptor for the resource view that encodes
    // the given buffer stride.
    auto bufferResImpl = checked_cast<BufferImpl*>(view->m_resource.get());
    auto desc = view->m_desc;
    SLANG_RETURN_ON_FAIL(createD3D12BufferDescriptor(
        bufferResImpl,
        checked_cast<BufferImpl*>(view->m_counterResource.get()),
        desc,
        bufferStride,
        device,
        m_allocator,
        &outDescriptor
    ));
    m_mapBufferStrideToDescriptor[bufferStride] = outDescriptor;

    return SLANG_OK;
}

Result ResourceViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12CpuDescriptorHandle;
    outHandle->value = m_descriptor.cpuHandle.ptr;
    return SLANG_OK;
}
#endif


} // namespace rhi::d3d12
