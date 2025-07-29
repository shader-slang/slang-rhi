#include "d3d11-buffer.h"
#include "d3d11-device.h"
#include "d3d11-utils.h"

namespace rhi::d3d11 {

BufferImpl::BufferImpl(Device* device, const BufferDesc& desc)
    : Buffer(device, desc)
{
}

DeviceAddress BufferImpl::getDeviceAddress()
{
    return 0;
}

ID3D11ShaderResourceView* BufferImpl::getSRV(Format format, const BufferRange& range)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    ViewKey key = {format, range};

    std::lock_guard<std::mutex> lock(m_mutex);

    ComPtr<ID3D11ShaderResourceView>& srv = m_srvs[key];
    if (srv)
        return srv.get();

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Format = getFormatMapping(format).srvFormat;

    if (m_desc.elementSize)
    {
        srvDesc.Buffer.FirstElement = UINT(range.offset / m_desc.elementSize);
        srvDesc.Buffer.NumElements = UINT(range.size / m_desc.elementSize);
    }
    else if (format == Format::Undefined)
    {
        // We need to switch to a different member of the `union`,
        // so that we can set the `BufferEx.Flags` member.
        //
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;

        srvDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
        srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        srvDesc.BufferEx.FirstElement = UINT(range.offset / 4);
        srvDesc.BufferEx.NumElements = UINT(range.size / 4);
    }
    else
    {
        const FormatInfo& formatInfo = getFormatInfo(format);
        srvDesc.Buffer.FirstElement = UINT(range.offset / (formatInfo.blockSizeInBytes / formatInfo.pixelsPerBlock));
        srvDesc.Buffer.NumElements = UINT(range.size / (formatInfo.blockSizeInBytes / formatInfo.pixelsPerBlock));
    }

    SLANG_RETURN_NULL_ON_FAIL(device->m_device->CreateShaderResourceView(m_buffer, &srvDesc, srv.writeRef()));

    return srv.get();
}

ID3D11UnorderedAccessView* BufferImpl::getUAV(Format format, const BufferRange& range)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    ViewKey key = {format, range};

    std::lock_guard<std::mutex> lock(m_mutex);

    ComPtr<ID3D11UnorderedAccessView>& uav = m_uavs[key];
    if (uav)
        return uav.get();

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Format = getFormatMapping(format).srvFormat;

    if (m_desc.elementSize)
    {
        uavDesc.Buffer.FirstElement = UINT(range.offset / m_desc.elementSize);
        uavDesc.Buffer.NumElements = UINT(range.size / m_desc.elementSize);
    }
    else if (format == Format::Undefined)
    {
        uavDesc.Buffer.Flags |= D3D11_BUFFER_UAV_FLAG_RAW;
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.Buffer.FirstElement = UINT(range.offset / 4);
        uavDesc.Buffer.NumElements = UINT(range.size / 4);
    }
    else
    {
        const FormatInfo& formatInfo = getFormatInfo(format);
        uavDesc.Buffer.FirstElement = UINT(range.offset / (formatInfo.blockSizeInBytes / formatInfo.pixelsPerBlock));
        uavDesc.Buffer.NumElements = UINT(range.size / (formatInfo.blockSizeInBytes / formatInfo.pixelsPerBlock));
    }

    SLANG_RETURN_NULL_ON_FAIL(device->m_device->CreateUnorderedAccessView(m_buffer, &uavDesc, uav.writeRef()));

    return uav.get();
}

Result DeviceImpl::createBuffer(const BufferDesc& desc_, const void* initData, IBuffer** outBuffer)
{
    BufferDesc desc = fixupBufferDesc(desc_);

    auto d3dBindFlags = _calcResourceBindFlags(desc.usage);

    size_t alignedSizeInBytes = desc.size;

    if (d3dBindFlags & D3D11_BIND_CONSTANT_BUFFER)
    {
        // Make aligned to 256 bytes... not sure why, but if you remove this the tests do fail.
        alignedSizeInBytes = math::calcAligned2(alignedSizeInBytes, 256);
    }

    // Hack to make the initialization never read from out of bounds memory, by copying into a buffer
    std::vector<uint8_t> initDataBuffer;
    if (initData && alignedSizeInBytes > desc.size)
    {
        initDataBuffer.resize(alignedSizeInBytes);
        ::memcpy(initDataBuffer.data(), initData, desc.size);
        initData = initDataBuffer.data();
    }

    D3D11_BUFFER_DESC bufferDesc = {0};
    bufferDesc.ByteWidth = UINT(alignedSizeInBytes);
    bufferDesc.BindFlags = d3dBindFlags;
    bufferDesc.CPUAccessFlags = _calcResourceAccessFlags(desc.memoryType);
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;

    // If buffer will be used for upload, then:
    //  - if pure copying, create as a staging buffer (D3D11_USAGE_STAGING)
    //  - if not, create as a dynamic buffer (D3D11_USAGE_DYNAMIC) unless unordered access is specified
    if (desc.memoryType == MemoryType::Upload)
    {
        bufferDesc.CPUAccessFlags |= D3D11_CPU_ACCESS_WRITE;
        if ((desc.usage & (BufferUsage::CopySource | BufferUsage::CopyDestination)) == desc.usage)
        {
            bufferDesc.Usage = D3D11_USAGE_STAGING;
            bufferDesc.CPUAccessFlags |= D3D11_CPU_ACCESS_READ; // Support read, so can be mapped as read/write
        }
        else if (!is_set(desc.usage, BufferUsage::UnorderedAccess))
        {
            bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        }
    }

    // If buffer will be used for read-back, then it must be staging.
    if (desc.memoryType == MemoryType::ReadBack)
    {
        bufferDesc.CPUAccessFlags |= D3D11_CPU_ACCESS_READ;
        bufferDesc.Usage = D3D11_USAGE_STAGING;
    }

    if (is_set(desc.usage, BufferUsage::IndirectArgument))
    {
        bufferDesc.MiscFlags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
    }

    switch (desc.defaultState)
    {
    case ResourceState::ConstantBuffer:
    {
        // We'll just assume ConstantBuffers are dynamic for now
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        break;
    }
    default:
        break;
    }

    if (bufferDesc.BindFlags & (D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE))
    {
        // desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        if (desc.elementSize != 0)
        {
            bufferDesc.StructureByteStride = (UINT)desc.elementSize;
            bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        }
        else
        {
            bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        }
    }

    D3D11_SUBRESOURCE_DATA subresourceData = {0};
    subresourceData.pSysMem = initData;

    RefPtr<BufferImpl> buffer(new BufferImpl(this, desc));

    SLANG_RETURN_ON_FAIL(
        m_device->CreateBuffer(&bufferDesc, initData ? &subresourceData : nullptr, buffer->m_buffer.writeRef())
    );
    buffer->m_d3dUsage = bufferDesc.Usage;

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    D3D11_MAP mapType;

    switch (mode)
    {
    case CpuAccessMode::Read:
        mapType = D3D11_MAP_READ;
        break;
    case CpuAccessMode::Write:
        // For upload, map as read-write if a staging buffer, or write-discard if dynamic buffer
        mapType = bufferImpl->m_d3dUsage == D3D11_USAGE_STAGING ? D3D11_MAP_READ_WRITE : D3D11_MAP_WRITE_DISCARD;
        break;
    default:
        return SLANG_E_INVALID_ARG;
    }

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    SLANG_RETURN_ON_FAIL(m_immediateContext->Map(bufferImpl->m_buffer, 0, mapType, 0, &mappedResource));
    *outData = mappedResource.pData;
    return SLANG_OK;
}

Result DeviceImpl::unmapBuffer(IBuffer* buffer)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    m_immediateContext->Unmap(bufferImpl->m_buffer, 0);
    return SLANG_OK;
}

} // namespace rhi::d3d11
