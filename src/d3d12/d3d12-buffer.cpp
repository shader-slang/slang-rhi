#include "d3d12-buffer.h"
#include "d3d12-device.h"

namespace rhi::d3d12 {

BufferImpl::BufferImpl(DeviceImpl* device, const BufferDesc& desc)
    : Buffer(desc)
    , m_device(device)
    , m_defaultState(D3DUtil::getResourceState(desc.defaultState))
{
}

BufferImpl::~BufferImpl()
{
    for (auto& srv : m_srvs)
    {
        if (srv.second)
        {
            m_device->m_cpuCbvSrvUavHeap->free(srv.second);
        }
    }
    for (auto& uav : m_uavs)
    {
        if (uav.second)
        {
            m_device->m_cpuCbvSrvUavHeap->free(uav.second);
        }
    }

    if (m_sharedHandle)
    {
        ::CloseHandle((HANDLE)m_sharedHandle.value);
    }
}

DeviceAddress BufferImpl::getDeviceAddress()
{
    return (DeviceAddress)m_resource.getResource()->GetGPUVirtualAddress();
}

Result BufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12Resource;
    outHandle->value = (uint64_t)m_resource.getResource();
    return SLANG_OK;
}

Result BufferImpl::getSharedHandle(NativeHandle* outHandle)
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

D3D12_CPU_DESCRIPTOR_HANDLE BufferImpl::getSRV(Format format, uint32_t stride, const BufferRange& range)
{
    ViewKey key = {format, stride, range, nullptr};
    CPUDescriptorAllocation& allocation = m_srvs[key];
    if (allocation)
        return allocation.cpuHandle;

    D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
    viewDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    viewDesc.Format = D3DUtil::getMapFormat(format);
    viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (stride)
    {
        viewDesc.Buffer.FirstElement = range.offset / stride;
        viewDesc.Buffer.NumElements = UINT(range.size / stride);
        viewDesc.Buffer.StructureByteStride = stride;
    }
    else if (format == Format::Unknown)
    {
        viewDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        viewDesc.Buffer.FirstElement = range.offset / 4;
        viewDesc.Buffer.NumElements = UINT(range.size / 4);
        viewDesc.Buffer.Flags |= D3D12_BUFFER_SRV_FLAG_RAW;
    }
    else
    {
        const FormatInfo& formatInfo = getFormatInfo(format);
        SLANG_RHI_ASSERT(formatInfo.pixelsPerBlock == 1);
        viewDesc.Buffer.FirstElement = range.offset / formatInfo.blockSizeInBytes;
        viewDesc.Buffer.NumElements = UINT(range.size / formatInfo.blockSizeInBytes);
    }

    allocation = m_device->m_cpuCbvSrvUavHeap->allocate();
    m_device->m_device->CreateShaderResourceView(m_resource.getResource(), &viewDesc, allocation.cpuHandle);

    return allocation.cpuHandle;
}

D3D12_CPU_DESCRIPTOR_HANDLE BufferImpl::getUAV(
    Format format,
    uint32_t stride,
    const BufferRange& range,
    BufferImpl* counter
)
{
    ViewKey key = {format, stride, range, counter};
    CPUDescriptorAllocation& allocation = m_uavs[key];
    if (allocation)
        return allocation.cpuHandle;

    D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc = {};
    viewDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    viewDesc.Format = D3DUtil::getMapFormat(format);
    if (stride)
    {
        viewDesc.Buffer.FirstElement = range.offset / stride;
        viewDesc.Buffer.NumElements = UINT(range.size / stride);
        viewDesc.Buffer.StructureByteStride = stride;
    }
    else if (format == Format::Unknown)
    {
        viewDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        viewDesc.Buffer.FirstElement = range.offset / 4;
        viewDesc.Buffer.NumElements = UINT(range.size / 4);
        viewDesc.Buffer.Flags |= D3D12_BUFFER_UAV_FLAG_RAW;
    }
    else
    {
        const FormatInfo& formatInfo = getFormatInfo(format);
        SLANG_RHI_ASSERT(formatInfo.pixelsPerBlock == 1);
        viewDesc.Buffer.FirstElement = range.offset / formatInfo.blockSizeInBytes;
        viewDesc.Buffer.NumElements = UINT(range.size / formatInfo.blockSizeInBytes);
    }

    allocation = m_device->m_cpuCbvSrvUavHeap->allocate();
    ID3D12Resource* counterResource = counter ? counter->m_resource.getResource() : nullptr;
    m_device->m_device
        ->CreateUnorderedAccessView(m_resource.getResource(), counterResource, &viewDesc, allocation.cpuHandle);

    return allocation.cpuHandle;
}

Result DeviceImpl::mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    SLANG_RETURN_ON_FAIL(bufferImpl->m_resource.getResource()->Map(0, nullptr, outData));
    return SLANG_OK;
}

Result DeviceImpl::unmapBuffer(IBuffer* buffer)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    bufferImpl->m_resource.getResource()->Unmap(0, nullptr);
    return SLANG_OK;
}

} // namespace rhi::d3d12
