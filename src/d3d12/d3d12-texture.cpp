#include "d3d12-texture.h"

namespace rhi::d3d12 {

TextureImpl::TextureImpl(const TextureDesc& desc)
    : Parent(desc)
    , m_defaultState(D3DUtil::getResourceState(desc.defaultState))
{
}

TextureImpl::~TextureImpl()
{
    if (sharedHandle)
    {
        CloseHandle((HANDLE)sharedHandle.value);
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
    if (sharedHandle != 0)
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

} // namespace rhi::d3d12
