#include "d3d12-buffer.h"

namespace rhi::d3d12 {

BufferImpl::BufferImpl(const BufferDesc& desc)
    : Parent(desc)
    , m_defaultState(D3DUtil::getResourceState(desc.defaultState))
{
}

BufferImpl::~BufferImpl()
{
    if (sharedHandle)
    {
        CloseHandle((HANDLE)sharedHandle.value);
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
    if (sharedHandle)
    {
        *outHandle = sharedHandle;
        return SLANG_OK;
    }

    // If a shared handle doesn't exist, create one and store it.
    ComPtr<ID3D12Device> pDevice;
    auto pResource = m_resource.getResource();
    pResource->GetDevice(IID_PPV_ARGS(pDevice.writeRef()));
    SLANG_RETURN_ON_FAIL(
        pDevice->CreateSharedHandle(pResource, NULL, GENERIC_ALL, nullptr, (HANDLE*)&sharedHandle.value)
    );
    sharedHandle.type = NativeHandleType::Win32;
    *outHandle = sharedHandle;
    return SLANG_OK;
#endif
}

Result BufferImpl::map(MemoryRange* rangeToRead, void** outPointer)
{
    D3D12_RANGE range = {};
    if (rangeToRead)
    {
        range.Begin = (SIZE_T)rangeToRead->offset;
        range.End = (SIZE_T)(rangeToRead->offset + rangeToRead->size);
    }
    SLANG_RETURN_ON_FAIL(m_resource.getResource()->Map(0, rangeToRead ? &range : nullptr, outPointer));
    return SLANG_OK;
}

Result BufferImpl::unmap(MemoryRange* writtenRange)
{
    D3D12_RANGE range = {};
    if (writtenRange)
    {
        range.Begin = (SIZE_T)writtenRange->offset;
        range.End = (SIZE_T)(writtenRange->offset + writtenRange->size);
    }
    m_resource.getResource()->Unmap(0, writtenRange ? &range : nullptr);
    return SLANG_OK;
}

} // namespace rhi::d3d12
